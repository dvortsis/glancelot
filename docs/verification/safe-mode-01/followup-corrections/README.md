# SAFE-MODE-01 second review corrections

The independent read-only Sonnet5.5 review of7bffd68 completed successfully (37 turns,176200ms); its raw JSON, prompt, model identity and report are retained under continued-corrections. It was conditionally favourable and required fixes/decisions before approval. No source change, test, command or additional agent was run by the reviewer.

Corrections in this candidate:

- F1: a deferred shortcut conflict restores the prior live binding when no newer local value replaced it. A private retry write whose supported primary is unchanged returns NoChange; ordinary updateSettings and recovered-primary repair semantics stay unchanged. The regression checks the live binding and exact primary/backup bytes after the rejected edit. Binding notification also guards owner deletion before emitting a subsequent signal.
- F2: layout reloads overlay this owner's queued value edits, restricted to the captured configuration namespace. Completion checks the slot, namespace and current saving feedback before changing feedback; a pending later write keeps the saving indication. Regressions leave/return to a slot, preserve newer locked-layout feedback, and switch configuration roots without leaking the old overlay.
- F3: callback supersession and teardown semantics are documented in the private header. Teardown warns when its total budget expires with edits left. Sort uses one stable channel containing all not-yet-saved changed fields; a sequence guards stale completion state. A burst of200 alternating key/direction changes stays within the queue cap and preserves foreign/seed fields.
- F4: a successful begin retry refreshes the recovery snapshot and replaces only the obsolete unavailable/failed-position feedback. A future-record-then-repair regression observes this recovery.
- F7 convenience: failed software-RHI proof now includes all three existing quit/report timing fields in its error message. Assertions, backend selection, frame/RGB requirements and timing budgets are unchanged.

The F5 versionless-source policy observation introduces no new change; layout merge semantics and policy hash are unchanged by this correction. F6 remains parser compatibility: no QT_QML_DEBUG or QQmlDebuggingEnabler is enabled in project source/configuration; Qt internals and optional developer debugging are not newly qualified. Package power-loss durability, non-UTF-8 filesystem paths and physical GPU/DPI/power-loss behavior remain unverified, as previously disclosed.

## Actual verification

Build: `& ./.cache/continued-corrections-build.ps1 -BuildDir build-codex-safe-mode-01`, restricting the candidate's standard environment/configure/build script to recovery_tests, recovery_startup_tests and recovery_controller_tests. Actual outputs: windows-build.txt and windows-final-build.txt.

With project-local Qt bin/Python Scripts on PATH:
`ctest --test-dir build-codex-safe-mode-01 -R '^(recovery|recovery_startup|recovery_controller)$' --output-on-failure`

Final actual CTest output (windows-final-focused.txt):
```
1/3 Test #34: recovery ......................... Passed 15.89 sec
2/3 Test #35: recovery_startup ................. Passed 0.53 sec
3/3 Test #36: recovery_controller .............. Passed 9.02 sec
100% tests passed out of 3
Total Test time (real) = 25.60 sec
```

Raw QtTest reports: recovery-results.txt137passed/0failed/0skipped15733ms; recovery-startup-results.txt61passed/0failed/0skipped387ms; recovery-controller-results.txt23passed/0failed/0skipped8624ms. The intermediate22-controller-case pass is separately retained in windows-first-controller-22.txt and windows-focused.txt. Python syntax validation: `.tools/python/Scripts/python.exe -m py_compile scripts/verify-recovery.py`, exit0. `git diff --check` passed.

The obsolete7bffd68 Windows/Docker wrappers were cancelled while still queued, before either acquired the full-gate lock. Their actual waiting output and verified process cancellation record remain under continued-corrections; they are not failed or passed qualification. Fresh full gates and a further independent pinned review are still required on this corrected source. No shared include/glancelot header, root CMake, queue, other lane worktree or main was changed.
