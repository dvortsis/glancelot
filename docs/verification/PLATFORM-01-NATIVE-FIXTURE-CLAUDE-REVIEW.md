# PLATFORM-01 native fixture review (Claude) - baseline 73fa157, prior report 130da47

Scope: PLATFORM native qualification and test-fixture diagnosis only. I read no FS-WATCH implementation, user media, credentials or private Explorer location data. From the other worktree I read only the allowlisted `qt-dispatcher-probe` files (`CMakeLists.txt`, `probe.cpp`, `run_fixture_probe.py`, and the three `*-results.json`). Tracked source in this tree is unchanged.

## Verdict
CHANGES REQUIRED stands. The integrated Windows gate remains failed. This is a proposal and diagnosis review, not approval of unimplemented code.

## Sources reviewed
- `src/app/platform_integration.cpp` (baseline).
  - Windows `revealInFileManager`: a directory calls `QProcess::startDetached("explorer.exe", {native})`. A file uses `CoInitializeEx`, `ILCreateFromPathW`, `ILClone`/`ILRemoveLastID`/`ILFindLastID` and `SHOpenFolderAndSelectItems`. On failure it falls back to `explorer.exe /select, <native>`.
  - Windows `openTerminal`: `QStandardPaths::findExecutable("wt.exe")`, then `startDetached(wt, {"-d", dir})`. Otherwise it runs `powershell.exe -NoExit -Command "Set-Location -LiteralPath '<dir with '' doubled>'"`.
- `tests/platform_integration_tests.cpp`: the `#ifndef Q_OS_WIN` guards. Linux launchers are isolated. Several tests, such as `terminalFileParentAndLiteralPath` and `terminalOverrideAndMissingLaunchers`, call the real `openTerminal`. The Windows path therefore launches real `wt.exe` or `powershell.exe` with `-NoExit`, and nothing closes them.
- The standalone Qt probe results: `results.json`, `fixture-results.json`, `pumped-results.json`.

## Own control run (the only native execution this round)
I copied the allowlisted probe to `.cache/probe-ctrl` in my tree. `.cache` is untracked and the probe is not committed. I added one mode, `fixture-drained3s`, which is the existing `fixture-drained` mode with a 3000 ms pre-drain in place of 250 ms. I built it with my own build dir under `.cache/probe-ctrl/build`, using the project Qt and the VS environment.

Command, with `QT_QPA_PLATFORM=offscreen`:
`dispatcher_probe.exe <mode> 3`, two rounds each, alternating modes.

| Mode | Worst GUI heartbeat gap per trial (ms) |
|---|---|
| fixture-idle, round 1 | 14, 221, 266 |
| fixture-drained3s, round 1 | 139, 259, 321 |
| fixture-idle, round 2 | 147, 199, 463 |
| fixture-drained3s, round 2 | 158, 73, 135 |

Each trial generates 12,000 one-byte files; setup took 2.7–3.7 s. There is no model code. Machine context: 58 `explorer.exe` processes and an idle `ui_settings_tests.exe` from another lane.

Result: 4 of 12 trials exceed 250 ms in a stock Qt process after generating 12,000 files. A 3 s drain did not clearly remove the effect (259 and 321 ms occurred after it). The stalls are intermittent and well above the 10–16 ms baseline the author measured with no files. This confirms the author's point that `DirectoryEntryModel` is not required to reproduce >250 ms gaps. It does not identify the cause (Defender or filesystem activity, shell load, or something else). N is small and runs were not isolated.

## Findings
1. **The `directory_model` 250 ms oracle on a freshly populated 12,000-file temp folder has an environmental noise floor on this host.** The floor is above the limit in a meaningful fraction of runs and is independent of product code. This agrees with my earlier report: the oracle measures real GUI-thread unavailability, and it fires without any Glancelot code involved.
   - Consequence: that test cannot serve as a reliable pass/fail signal on this machine until the noise source is found or isolated. Raising thresholds, skipping, or adding arbitrary drains is not supported by the data. Neither the author's pumped-generation control nor my 3 s drain removed it.
2. **Windows `platform_integration_tests` has a real, unbounded desktop side effect, independent of whether it causes the latency.** Each gate run opens real Explorer windows and terminals that persist. That is a plausible load and confounder source, and it is also a test-hygiene defect. I did not show causation (58 Explorer processes is an observation only).
3. **The existing Windows assertions are weak.** They check that a launch returned true. They do not check which program received which arguments or which working directory, so the Unicode, spaces and literal-quote cases are not validated on Windows.

## Assessment of the lead's proposed launcher fixture
The design is sound and strengthens the assertions. It keeps production code and public API unchanged, and the Windows tests move from "something launched" to "exactly these arguments".

Recommended contract:

1. **Production TU unchanged, test TU seam.** Include Windows headers first, `#define` only `SHOpenFolderAndSelectItems` to a typed capture function with the exact `HRESULT` signature `(PCIDLIST_ABSOLUTE, UINT, PCUITEMID_CHILD_ARRAY, DWORD)`, then `#include "../src/app/platform_integration.cpp"`.
   - The macro is applied after `<shlobj.h>`, so the declaration is not rewritten.
   - The test TU must not also link the production object for that file, or there is an ODR/duplicate-symbol conflict. The root lead must freeze this CMake wiring before implementation.
   - Keep a Linux path unchanged as the lead states.
2. **Capture function** must record folder PIDL, child PIDL and flags, then return both `S_OK` and a failing `HRESULT` across tests, so the success path and the fallback to `explorer.exe /select,` are both exercised. Compare PIDLs by resolving them with `SHGetNameFromIDList(SIGDN_FILESYSPATH)` or `ILIsEqual` against PIDLs created from the expected paths. Do not compare raw bytes. Free every PIDL the capture receives only if the production code does not (it frees its own).
3. **COM.** Production calls `CoInitializeEx(APARTMENTTHREADED)` and `CoUninitialize` itself. The test thread must not already hold a conflicting (MTA) apartment, or `RPC_E_CHANGED_MODE` will make production take the no-COM branch. Assert the S_OK and S_FALSE cases on a known apartment, and still expect `SHOpenFolderAndSelectItems` to be called only when `ILCreateFromPathW` succeeds. The file must exist, because the production code returns false first if it does not.
4. **Fake launchers.** Copy a single GUI-subsystem stub as `explorer.exe`, `wt.exe` and `powershell.exe` into a temp directory.
   - Windows `CreateProcess` search order checks the application directory first, then the current directory, then the system directories, then `PATH`. A bare `"explorer.exe"` and `"powershell.exe"` in production can therefore resolve to the real `C:\Windows\explorer.exe` or `System32\...` before the test PATH is consulted. This is the biggest pitfall. Mitigations: place the stubs in the test executable's directory (the application directory is searched first), or verify empirically with a canary stub that writes its own path, and fail the test if the real binaries run. Do not rely on PATH alone.
   - `QStandardPaths::findExecutable("wt.exe")` does use `PATH`, so the missing-launcher case can be driven by PATH, but `powershell.exe` cannot be hidden from the production fallback by PATH, so a stub in the application directory is also what makes "wt missing" deterministic.
   - Isolate the process environment (set `PATH`, current directory and `TEMP` through a scoped guard that restores them), and run these tests with no parallel test that depends on them.
5. **Capture process.** The stub should be a GUI-subsystem executable (no console window) with no Qt event loop, so it starts and exits immediately.
   - It writes `GetCommandLineW()` split with `CommandLineToArgvW`, plus `GetCurrentDirectoryW`, to a generated JSON file, encoded as UTF-8 from UTF-16 so Unicode survives. Use an exclusive temp name per invocation (for example derived from a PID or counter passed in the environment), because `startDetached` returns before the child runs.
   - The test must poll for the file with a bounded timeout (not a fixed sleep) and then wait for process exit (open the PID from the JSON, if captured, or tolerate a short settle). Delete all captures and stubs in cleanup; fail the test if a stub is still running at teardown.
   - Quoting: `startDetached` builds a command line from the Qt arguments, so the exact literal arguments (for example `-NoExit`, `-Command`, `Set-Location -LiteralPath '...'`, `/select,`, the native path) must be asserted from `argv`, not from the raw command-line string. Keep one test with a path containing a single quote, spaces and non-BMP Unicode.
6. **Terminal cases to assert:** `wt.exe -d <native dir>`; `wt` missing -> `powershell.exe -NoExit -Command Set-Location -LiteralPath '<dir>'` with `'` doubled; file argument -> parent directory; neither present -> expect false and no launch. Add the explorer directory case (`explorer.exe <native dir>`).
7. **No real persistent shell UI** should be possible. Add a guard in the test that fails if any real `explorer.exe`/`wt.exe`/`powershell.exe` PID that did not exist before the test appears afterwards. This catches search-order leaks, and it is a cheap check.

## Limits
- I ran only the stock probe control above. I did not build or run `platform_integration_tests`, `scripts/build.ps1 -Test`, Docker, or any full suite. I did not implement or test the fixture.
- I did not inspect the Explorer inventory, the `CreateProcess` search-order claim empirically, or the capture-stub design. Those points are from documented Windows behavior and should be checked with the canary stub before relying on them.
- The latency cause remains unidentified. No machine setting, process or service was changed.

## Useful next action
1. Lead freezes the CMake seam for the test-TU include of `platform_integration.cpp` and the stub target.
2. Implement the fixture with the canary-stub search-order check first. Then reduce the real shell side effects in the Windows gate by running it.
3. In a quiet window, re-run the stock probe (`fixture-idle` and `fixture-drained3s`, N≥10) alongside an independent USER-call monitor, to decide whether the noise floor is the host. If it is, the `directory_model` latency oracle needs a host-qualification step. Do not weaken the 250 ms limit.
4. Then a new independent reviewer should approve the implemented fixture and run the full gate on the integrated commit.
