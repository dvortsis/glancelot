# PLATFORM-01 native fixture implementation review (Claude lane)

Candidate `90b4e68`, baseline `ae8bd92`. Static review only.

## Verdict: CHANGES REQUIRED

No runtime pass is claimed. I ran no build, CTest, Docker or native test. I read only this worktree: the frozen `docs/PLATFORM-01-PLAN.md` fixture section, `cmake/PlatformTestFixtures.cmake`, the earlier diagnosis review, and `git diff ae8bd92..HEAD`.

The native timing gates stay failed/unresolved. This review does not approve FS-WATCH or INPUT UI.

## Verified statically
- `git diff --quiet ae8bd92..HEAD -- src include` exits 0, so the production adapter and headers are unchanged.
- The diff touches only the root `CMakeLists.txt`, the four permitted test files, and the existing test file.
- The root CMake change swaps the `platform_integration_tests` link line for `include(cmake/PlatformTestFixtures.cmake)`. The fragment keeps the Linux link line unchanged.
- The adapter defines a typed `captureSelection` with the exact `HRESULT WINAPI (PCIDLIST_ABSOLUTE, UINT, PCUITEMID_CHILD_ARRAY, DWORD)` signature. It substitutes only `SHOpenFolderAndSelectItems`, after the Shell headers, then includes the exact production `.cpp`. The production object is not linked a second time.
- PIDLs are compared by `SIGDN_FILESYSPATH` rather than raw bytes. Only the `ILCombine` result is freed.
- COM balance is checked by probing `CoInitializeEx` for `S_OK`/`S_FALSE`.
- `verify()` asserts the captured argv as an array, cwd, module path, SHA-256, token, pid image identity and exit, `console == false`, and `window_count == 0`.
- The stub is a `WIN32` target with no `QCoreApplication`. It writes bounded JSON through `QSaveFile`.

## Findings

### F1 (blocking): `initTestCase` aborts at the `findExecutable("wt.exe")` assertion
`tests/platform_integration_tests.cpp:46` (`initTestCase`) fails before the canary and before any production call. This matches the author's report. `SearchPathW`, the hashes, CWD and PATH checks all pass, and only Qt's lookup fails.

I could not run anything, and the project-local Qt source is not in this worktree. Treat the following as a hypothesis to test.

- **Likely cause:** `QStandardPaths::findExecutable` with no `paths` argument reads `PATH` through Qt's environment helper, which may be a narrow, ANSI-code-page read (`qgetenv` / `fromLocal8Bit`).
  - The sandbox is `...glancelot native 🧪 space-XXXXXX`, and `bootstrap` puts it first in `PATH`.
  - A non-ANSI character such as U+1F9EA turns into `?` in that conversion, so the candidate path never exists.
  - The fixture's own wide-API checks (`GetEnvironmentVariableW`, `SearchPathW`) cannot see this. `CreateProcess` also resolves `explorer.exe` and `powershell.exe` through the application directory, so only `wt.exe` is affected.
- **Cheap discriminator (read-only):** in a throwaway probe, compare `QStandardPaths::findExecutable("wt.exe")` with `findExecutable("wt.exe", {sandbox})`, `QString::fromLocal8Bit(qgetenv("PATH"))` and `qEnvironmentVariable("PATH")`.
- **If confirmed, do not weaken non-BMP coverage.**
  - Non-BMP is still exercised by `directory` and `photo` (argv), and by the sandbox as CWD and module path.
  - Only the directory that `PATH` points at has to survive the lossy conversion.
  - Options, both needing lead sign-off because they change the frozen contract's sandbox shape:
    - (a) Put an ASCII-only launcher directory into `PATH` and the app-dir copies, with the non-BMP part kept in the nested target and CWD directories.
    - (b) Put `GetShortPathNameW(sandbox)` into `PATH`. This works only if 8.3 names are enabled on the volume, so fail closed if it returns empty or still contains non-ASCII.
  - Whichever option is chosen, keep an assertion that the production `findExecutable` result equals the expected sandbox `wt.exe`.
  - The wide-API self-checks (the `resolves()` pre-checks) should stay.
- **Product observation, not a fixture task:** if the hypothesis holds, production `openTerminal` cannot find `wt.exe` on a `PATH` entry with non-ANSI characters. It would then fall back to PowerShell. That is a possible product bug to record separately.

### F2 (medium): `qtDirectory()` silently degrades to the executable directory
`tests/native_platform_fixture.hpp:49` (`qtDirectory`).

- `GetModuleHandleW(L"Qt6Core.dll")` returns `nullptr` for a debug Qt (`Qt6Cored.dll`).
- `modulePath(nullptr)` then returns the EXE path, so `qtDirectory()` becomes the build directory without any error.
- `bootstrap` would then set `PATH` to `sandbox;builddir`, and the copied helpers could exit with `0xC0000135`.
- The child exit code is non-zero, so this fails closed, but with no diagnostic.
- Fix: return `{}` when the module handle is null. Have `bootstrap` verify that `Qt6Core.dll` and `Qt6Test.dll` are in that directory, or probe for the debug name too.
- All of `bootstrap`'s failures return silent codes 101–109 with no `qWarning`/stderr line. Add a reason string.

### F3 (medium): existing-STA COM test does not assert its own balance
`revealFileNativeSuccessBalancesExistingSta`, in `tests/platform_integration_tests.cpp`.

- The nested `CoInitializeEx` returning `S_FALSE` is true whether or not production leaked a reference. The outer apartment is still alive at that point.
- A leak would only show up in the next test's `verifyApartmentUninitialized`, and be attributed there.
- Fix: after the `Balance` object is released, assert `verifyApartmentUninitialized()` in this test. Scope the `Balance` object in a block.

### F4 (low): bounded-lifetime hygiene
- `bootstrap` checks `ActiveProcesses != 0` immediately after the child exit. A short bounded poll of up to about 1 s would avoid a flaky false 109 if job accounting lags the process handle.
- `verify()` waits for the helper pid to exit but never reads its exit code. The stub returns 91–95 on failure, so `GetExitCodeProcess == 0` should be asserted.
- A recycled pid maps to the "not own image" error, which is fail-closed. The error text, "did not exit or pid identity changed", is ambiguous.
- If the parent is killed (for example by a CTest timeout), `KILL_ON_JOB_CLOSE` stops the tree. The `QTemporaryDir` sandbox is not removed, so copied runner and stubs under `%TEMP%` accumulate. This is acceptable only if documented. A startup sweep of stale `glancelot native *` directories that no process holds would be better.
- `verify()` polls with a 5 ms `msleep` on the GUI-less test thread. This is fine, but `QTRY` style bounds are already 1.5 s, so a slow Defender scan of a freshly copied exe could flake. Report timings.

### F5 (low): coverage gaps against the frozen contract
- "Missing launcher failure" is replaced by an invalid-PE `powershell.exe` in the application directory. The comment is honest about this, but the claim depends on `CreateProcess` not continuing its search after `ERROR_BAD_EXE_FORMAT`. That is unverified, and the canary does not cover it. Assert the stub did not run, which the `noCapture()` check already does, and record the observed `startDetached` result at runtime.
- Not covered:
  - `CoInitializeEx` returning `RPC_E_CHANGED_MODE` (MTA test thread).
  - A `SHOpenFolderAndSelectItems` failure with no `explorer.exe` available.
  - The `ILCreateFromPathW`-null branch. This is unreachable for an existing file, so it is acceptable to omit.

### F6 (observations, no change required)
- `PATH` is `sandbox;qtDir` with forward slashes (from `QDir::canonical`). `System32` is removed from `PATH`. This is intentional isolation. Confirm at runtime that `SearchPathW` and the canary still pass with that form.
- The Windows branch of the test class replaces the old Windows-reachable assertions. I checked the original: the invalid/missing path, temp dir, Unicode dir and root-terminal cases are all covered by the new Windows tests with stronger assertions. No Windows-reachable coverage is lost.
- `std::wstring` in `environment()` relies on a transitive include. It compiled in the author's run.

## Required before re-review
1. Resolve F1 and show the discriminator output. Keep the non-BMP argv/CWD/module assertions, keep the `findExecutable` equality assertion, and get lead sign-off on any sandbox-shape change.
2. Apply F2, F3 and the exit-code assertion from F4.
3. Provide actual output for the serialized native run: platformtarget+helper build, bounded CTest with timing, and a leak check using only owned job and pid evidence. Do not use global process counts or stop unrelated processes.
4. Keep the 30 s CTest timeout and the 250 ms oracle unchanged.

Re-review will be static first, then run only after the author releases the native slot.
