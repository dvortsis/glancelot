# SAFE-MODE-01 pending sort-save correction

The first full Windows gate on61e4752 failed35/37. The exact window_slots failure was reproduced without modifying its existing test: contendedSortReportsSessionOnly requires session-only feedback while a settings writer lock prevents persistence. saveWithRetry returns acceptance for retry, so its return value cannot establish saved state.

The private controller now tracks pending and persisted states separately. Pending sort feedback says "for this session; saving settings is pending"; only a successful completion clears that qualification. Terminal failure retains session-only failure feedback. Sort worker results identify sort completion, and the GUI composes the message using current persistence state rather than a stale message captured before the worker ran. Deferred save completion updates only the sort feedback that still owns the status, preserving newer navigation/error feedback. No public header, root configuration, queue, watcher source, existing assertion or timing budget changes.

New behavioral coverage exercises pending-to-saved transitions with an empty and a loaded navigator, including the asynchronous sort completion, plus preservation of newer feedback. The existing failed-save,200-edit coalescing, foreign-field and recovery tests remain intact.

Actual focused commands:

```powershell
& ./.cache/continued-corrections-build.ps1 -BuildDir build-codex-safe-mode-01
$env:PATH='C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64/bin;C:/src/Github/Glancelot/.tools/python/Scripts;'+$env:PATH
ctest --test-dir build-codex-safe-mode-01 --output-on-failure -R '^(recovery|recovery_startup|recovery_controller)$'
```

The restricted build runner uses the standard MSVC/Qt setup and builds only recovery_tests, recovery_startup_tests and recovery_controller_tests; it does not run a full gate. Actual output in windows-build.txt/windows-focused.txt:

```text
1/3 Test #34: recovery ......................... Passed 15.17 sec
2/3 Test #35: recovery_startup ................. Passed 0.35 sec
3/3 Test #36: recovery_controller .............. Passed 13.17 sec
100% tests passed out of 3
Total Test time (real) = 28.93 sec
```

Raw QtTest results retain137 recovery,61 startup and29 controller passes (227 total), zero failures/skips. Fresh independent Claude source review and full locked Windows/Docker qualification remain required. The earlier fs_watch full-run failure is not explained by its isolated pass; both results remain in ../confirmed-binding-corrections. Physical GPU/DPI, sanitizer and power-loss tests remain unverified. No integration is performed by this lane.
