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

---

# Re-review of `6b7109e` (author source `038d1b5`), static only

Baseline `ae8bd92`. Prior report commit `56d27cb` is an ancestor of `6b7109e`. Only this report was edited. I ran no build, test, process stop or window close. The native slot belongs to the root `directory_model` diagnostic.

## Verdict: CONDITIONAL STATIC APPROVAL
This is not a runtime approval and not a gate pass.
- **Static:** I found no blocking defect. F1–F4 of my first review are addressed. Remaining items are non-blocking (R1–R5).
- **Runtime:** no independent run exists. The author's 9/9 and CTest 0.70 s results are author-reported evidence only.
- **Still unresolved:** the full native gate and the strict 250 ms `directory_model` oracle stay failed/unresolved. Docker is not run. FS-WATCH and INPUT UI are not approved.

## Checked
- `git diff --quiet ae8bd92 6b7109e -- src include` exits 0, so the production adapter and headers are unchanged.
- Changes since `90b4e68` are limited to `tests/native_platform_fixture.hpp`, `tests/platform_integration_tests.cpp` and `tests/platform_launcher_stub.cpp`. CMake and the adapter are untouched.

### Prior findings
- **F1 (`wt.exe` lookup):**
  - `shortDirectory()` takes the `GetShortPathNameW` result and rejects it if any character is above U+007F. It also opens the original and the alias and compares volume serial and file index. It returns empty otherwise, and `bootstrap` fails closed with code 110.
  - The same Unicode sandbox stays the CWD, module and target; only `PATH` carries the alias. `prepare()` re-checks `PATH == shortDirectory(sandbox);qtDir`.
  - `samePath` now compares volume and file ID, so Qt's non-expanded 8.3 strings compare correctly. This also covers `resolves()` and the stub's own-directory check.
  - Non-BMP coverage is retained: the module path, CWD, `directory`, `photo` and argv are all non-BMP.
  - Your cited Qt 6.8.3 `qstandardpaths.cpp` (default lookup via `qgetenv` and `fromLocal8Bit`) is consistent with the earlier hypothesis.
- **F2 (Qt directory):** `qtDirectory()` tries `Qt6Core.dll`, then `Qt6Cored.dll`, and returns empty rather than the EXE directory. `bootstrap` fails closed with 110. All bootstrap failures now go through `failure(code, reason)` to stderr.
- **F3 (existing STA):** the `Balance` object is scoped in a block, then `verifyApartmentUninitialized()` runs in the same test.
- **F4:** the job-accounting poll is bounded at 1 s. The helper exit code is checked in `verify()`. The stub records its `GetProcessTimes` creation time as `started`, and the verifier compares it with the pid's creation time, which catches pid reuse. The `QueryFullProcessImageName` approach failed with 31 on an exited helper, and the creation-time check replaces it correctly.
- **Default QtTest logger and error mode:**
  - When there is no `-o`, `bootstrap` adds `-o test-output.txt,txt` (relative, in the unchanged Unicode CWD). Explicit `-o` values are still made absolute.
  - After `CloseHandle(job)` it replays the log to the parent's stdout.
  - `qExec(&tests, app.arguments())` is used on Windows only; Linux is unchanged.
  - `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)` runs after the bootstrap branch. It is inherited by the helpers and explains the earlier 22 s timeout from the deliberate invalid image.
- **Author evidence read:** `.cache/author-platform-detail.txt` shows 9 passed in 672 ms (Qt 6.8.3, MSVC 2022). `.cache/author-platform-ctest.log` shows `Passed 0.70 sec`, total 0.75 s. I could not independently reproduce them. The logs do not record a commit SHA, and the CTest log's build directory is the author's other worktree (`codex-fs-watch-01`). Their linkage to `6b7109e` is by the author's statement only.

## Remaining items (non-blocking; fix or accept explicitly)
- **R1 (diagnostic message in `initTestCase`):**
  - The long `QVERIFY2` message is built eagerly, so it calls `findExecutable` three times and formats `PATH` on every run, pass or fail.
  - Build the message only on failure (`if(!ok) QFAIL(...)`), or drop `QtPATH`/`nativePATH`.
- **R2 (log replay):**
  - A log over 256 KiB is not replayed at all, and only a passing result is turned into 111. A failing run with a large log hides its diagnostics.
  - Replay the first 256 KiB and note truncation.
  - `fwrite` in stdout text mode doubles CR in an already-CRLF log (`\r\r\n`). This is cosmetic. Set binary mode or strip CR.
- **R3 (misleading final message):**
  - `if(result) failure(result,"copied runner failed, timed out or retained owned processes")` also fires for an ordinary QtTest failure count. It prints a stale `GetLastError`.
  - Print this only for codes 106–111, or say "child exit code N".
- **R4 (exit-code check is weaker than it looks):** `QProcess::startDetached` closes its handles. For a quick helper, `OpenProcess` usually fails with `ERROR_INVALID_PARAMETER`. The code accepts that branch, with no exit-code or creation-time check. In that case the helper's exit is inferred from the capture file having been committed (the stub returns 0 right after a successful commit). The creation-time and exit-code checks run only while the process object still exists. This is acceptable if documented. Do not describe it as an exit-code proof in handoffs.
- **R5 (environmental precondition):** the test needs 8.3 short names on the sandbox volume. Without them the run fails closed with 110 rather than skipping. That is the right behavior. Record the precondition in the handoff, and say the 110 message should mention 8.3.

## Limitations to document (explicit)
- **Missing-launcher coverage:** this remains the invalid-executable `powershell.exe` substitute and does not prove "system PowerShell absent". No source/product seam outside the contract is required. Whether `CreateProcess` continues its search after a bad image was not observed by me.
- **Parent kill and TEMP files:** if the bootstrap parent is killed (for example by a CTest timeout), `KILL_ON_JOB_CLOSE` terminates the owned process tree, but the private `%TEMP%\glancelot native 🧪 space-*` directory is not removed. It may leave inert private copied files. I do not require a glob sweep because ownership of other such directories is unknown.
- **Bad Image dialog:** the earlier invalid-image hang suggests a hard-error dialog. I did not verify whether a dialog from the earlier unmitigated run remained open. I did not touch any window.

## Needed before approval
1. Independent runtime run of `platform_integration` (bounded CTest plus its log) by a reviewer in a free native slot, with an owned-job and pid leak check. Do not use global process counts.
2. Author decision or fix on R1–R3, and documentation of R4 and R5.
3. Full native and Docker gates on the integrated commit under the project's integration rules. The strict 250 ms `directory_model` oracle and the 30 s deadline stay unchanged and unrelaxed.
