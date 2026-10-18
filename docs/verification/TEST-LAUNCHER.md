# Windows testing launcher verification

Scope: native Windows testing shortcuts and scripts only; no viewer UI or shared C++ contracts changed.

Independent reviewer approved Windows PowerShell 5.1 parsing, missing manifest rejection, failed build stopping launch, successful manifest/reopen, viewer tamper rejection, prior manifest removal after failed rebuild, checkout/photo paths with spaces, isolated launch config and parent environment restoration. Reviewer used disposable stubs and did not substitute those checks for the real native build.

Lead failure checks (actual Windows PowerShell child processes):

```text
Missing photo exit=1
No successful package exit=1
No successful test build is available. Double-click Test Glancelot.cmd first.
```

Isolated build/test/package completed successfully (Windows PowerShell 5.1, MSVC2022, Qt 6.8.3):

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-app.ps1 -NoLaunch
100% tests passed out of 11
Total Test time (real) = 289.27 sec
process exit=0
```

Real fast-launch smoke with generated `test photo Ω.png` (96x64) succeeded. The process remained alive, then closed through CloseMainWindow/WaitForExit. The requested Unicode/space path was saved in `.cache/manual-testing/config/settings.json` in the assigned worktree. Source photo SHA256 before/after was `F5AECE7B5E6D8CCED1871DF1B85CED39A5CA7E37C5113A9F63C9BDFC2170CA54`.

Integrated main commit `8cd4b91` passed the complete launcher workflow:

```text
100% tests passed out of 11
Total Test time (real) = 122.53 sec
Test build: main @ 8cd4b91195c12606a0ea1d1d1c05e0b1825cee90
Local changes: False
process exit=0
Integrated fast launcher opened and closed normally.
Timed packaged smoke: exit=0, size=96x64, loading=False, Qt=6.8.3
```

The requested image was saved under the main checkout's isolated test config; its source hash remained unchanged. `.cache/manual-testing/report.json` confirmed actual packaged decoding. No development Qt path was added during these launches.

Actual CMD invocation from PowerShell 7 revealed that its inherited PSModulePath can hide Windows PowerShell 5.1 modules. Follow-up `3bcef86` selects the explicit SystemRoot Windows PowerShell executable and prepends its builtin module directory inside setlocal. Independent review verified both real wrappers through CMD with paths containing spaces and working SHA256/Get-FileHash; both returned zero. No viewer runtime or PowerShell launcher script changed after the full integrated tests.

Build logs: `.cache/test-launcher-build.log` and `.cache/test-launcher-integrated.log` (ignored local evidence).

Physical monitor/color accuracy, clean-machine deployment and Linux are outside this launcher-only verification. Docker deployment is unchanged.

## Final current-version integration

A concurrent pending folder-jump merge conflicted only at the additive work-queue entries. Both task entries and the preexisting staged folder-jump implementation were preserved in merge `3e06e78`, together with the reviewed launcher wrapper fix. No other task was marked complete.

The complete default launcher was rerun on this clean integrated commit:

```text
100% tests passed out of 11
Total Test time (real) = 185.41 sec
Test build: main @ 3e06e787054e1a4565152581f332e23075f9610c
Local changes: False
process exit=0
Final CMD fast shortcut exit=0
Final smoke: exit=0, size=96x64, loading=False
```

Final build log: `.cache/test-launcher-final-integrated.log`; packaged runtime report: `.cache/manual-testing/final-report.json`. The final completion commit changes only verification and this task's work-queue state. The portable package remains the successfully tested `3e06e78` runtime.
