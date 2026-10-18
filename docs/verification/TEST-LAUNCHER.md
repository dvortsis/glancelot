# Windows testing launcher verification

Scope: native Windows testing shortcuts and scripts only; no viewer UI or shared C++ contracts changed.

Independent reviewer approved Windows PowerShell 5.1 parsing, missing manifest rejection, failed build stopping launch, successful manifest/reopen, viewer tamper rejection, prior manifest removal after failed rebuild, checkout/photo paths with spaces, isolated launch config and parent environment restoration. Reviewer used disposable stubs and did not substitute those checks for the real native build.

Lead failure checks (actual Windows PowerShell child processes):

```text
Missing photo exit=1
No successful package exit=1
No successful test build is available. Double-click Test Glancelot.cmd first.
```

Native build/test/package, integrated checks and real packaged launch evidence are pending.

Physical monitor/color accuracy, clean-machine deployment and Linux are outside this launcher-only verification. Docker deployment is unchanged.
