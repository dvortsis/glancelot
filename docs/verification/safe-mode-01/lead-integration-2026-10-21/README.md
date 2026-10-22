# SAFE integration round outcome,2026-10-21

**Not merged. SAFE returns to review, blocked on STARTUP-WINDOW-01.** Independent source acceptance is retained, but the mandatory synchronized Windows process gate failed and repeated in isolation. The user assigned this session lead/integrator. No lane implementation fix, weakened oracle, changed renderer, increased timeout or integration waiver was introduced.

## Exact source and review

Reviewed SAFE evidence116c6ba merged locked main cd26e0c cleanly in7e8897975d507c29907d6a026792e1ea42b7cedd. safe-source-equality.txt records zero SAFE product/test/recovery-contract differences from qualified796ab411 plus exact sync-only files. All runtime evidence here uses7e88979. Later evidence-only commits do not qualify changed source.

Actual independent Sonnet5.5 final source review accepted7e88979 with no required source changes: session f035406f-cdc5-4062-8450-6f5e4cb01919,44turns,203852ms,exit0. CLAUDE-FINAL-REVIEW-7e88979.md and raw JSON/prompt/command/status retain scope and limits. LEAD-DECISIONS.md records original High/Medium closure and nonblocking residuals. Source acceptance is not runtime integration approval.

## Full synchronized gates: actual output

integration-gate-command.ps1.txt is the executed script. Both full commands ran in one PowerShell process inside the main checkout's gate-lock.ps1 -Branch codex/safe-mode-01 -Run block; it waited for the live Claude mainsync gate. Docker LASTEXITCODE is recorded and checked explicitly. Actual output is windows-full.txt/docker-full.txt, plus terminal gate-status.txt/integration-script-output.txt.

```text
Windows:
19/37 fs_watch Passed 82.58 sec
20/37 fs_watch_budget Passed 107.27 sec
34/37 recovery Passed 14.41 sec
35/37 recovery_startup Passed 0.27 sec
36/37 recovery_controller Passed 12.94 sec
37/37 recovery_process Failed 9.12 sec
97% tests passed, 1 tests failed out of 37
Total Test time (real) = 601.99 sec
windows_gate_exit=1

Docker:
19/37 fs_watch Passed 68.98 sec
20/37 fs_watch_budget Passed 22.42 sec
34/37 recovery Passed 10.96 sec
35/37 recovery_startup Passed 0.04 sec
36/37 recovery_controller Passed 11.48 sec
37/37 recovery_process Passed 23.11 sec
100% tests passed, 0 tests failed out of 37
Total Test time (real) = 283.69 sec
docker_gate_exit=0
integration_gate_exit=1
```

Windows component/raw CTest logs and failed26-command process run4dl0bn3q are retained. Linux raw logs and full35-command passed=true proof run-luj861te are under linux-raw. The separately locked build-stage export completed0, explicitly reused CACHED compile/CTest layers and is not a second fresh test pass. Its temporary evidence container was removed. linux-process-proof-status.txt and linux-evidence-export.txt retain actual results. Physical GPU/DPI accuracy, input-to-photon, sanitizer and power-loss qualification remain unverified.

## Failure and focused diagnostics

- Full Windows run4dl0bn3q: first safe GUI command26 decoded the disposable RGB image, but frames0, first_image_frame_ms=-1, exposed=false, observed RHI=false after one render callback and one invalidation. The visible/active window reported2360x1560 on the96DPI primary screen. Its screenshot is not substituted for actual swaps.
- Identical unchanged isolated CTest:1/1 failed in5.97s, exit8; run-mf3dp8ac again stopped at command26 with the same frame/exposure/geometry state. All original renderer/deadline/frame/RGB conditions remained intact. Raw output and artifacts are windows-process-isolated.txt/windows-isolated-process.
- Independent read-only diagnosis: actual Sonnet5.5 session0a6b275c-b33a-4510-a9d4-4321d8773080,21turns,103930ms,exit0. CLAUDE-NATIVE-DIAGNOSIS.md keeps its hypotheses and limits; it recommends unchanged reproduction, then native timeline evidence before changing source. No inference in that report alone proves historical causality.
- Built-in Qt logging plus allowlisted actual child Qt environment was added by an external runpy wrapper without editing product/tests/verifier. First logging run-a5oxvwzl failed at command26; stderr was empty. Qt's pinned default Windows logger may route away from stderr. Both failed artifacts and the precise wrapper are retained.
- Routing those same diagnostic messages to captured stderr with QT_FORCE_STDERR_LOGGING=1 produced run-dfrnwowm. Command26 passed22swaps at1180x780/exposed=true, then command27 failed0swaps at2360x1560/exposed=false. The oracle still failed overall (exit1,27commands), not a passing process proof. Diagnostic logging may affect timing; no performance qualification is inferred.

## Concrete offscreen placement evidence

windows-qtstderr-process/command-027.stderr.txt records both screens, then a native creation rectangle(0,1600,2360,1560). The primary physical screen is(0,0,2560,1440); the secondary is(0,-1600,2560,1600),192DPI. The failed rectangle is wholly below both. Qt logged the caller's logical request(0,0,1180,780) but already supplied the offscreen/doubled rectangle to CreateWindowEx, before showing. There is no expose event before the quit callback; the RHI is created only for the late grab. The paired passing command26 was created at(690,306,1180,780) and got an expose/render loop.

The recorded child scale-factor/font-DPI overrides are null in this logging reproduction. They do not establish the old failing child's environment. The user could not confirm whether the desktop was locked/asleep/disconnected at the original failure; its state stays unknown. A screenshot-only render is explained by the public Qt path in QT-SOURCE-CHECK.md; the earlier Qt/OS decision that produced the native offscreen geometry is not established. The placement mechanism is concrete enough to freeze defensive startup recovery and deterministic tests without blaming a desktop state or changing renderer policy.

## Lead disposition and next lane work

SAFE remains unmerged and not done. The new lead-owned STARTUP-WINDOW-01 plan/private API/test wiring freezes a Codex correction for wholly offscreen startup geometry, bounded pre-grab position/exposure diagnostics, and dedicated pure-value tests. The normal/safe35-command oracle, actual swap/device/RGB assertions, all deadlines and viewer minimum controls must remain intact. This packet runs alone in the Codex implementation lane, stacked on reviewed SAFE plus the main contract. It requires independent actual Sonnet review, mandatory test wiring and fresh complete Windows/Docker gates before combined integration. Watcher harness timing work stays deferred behind this blocker.

INPUT87b063d and MONITORae1b7e4 remain CHANGES REQUIRED; their exact independent reports/raw outputs were imported docs-only to main in cd26e0c, with owner-lane fixes requested. No product branches were merged. The lead lock is released only after this evidence and the concrete correction packet are recorded on main.

Contract close-out: initial Sonnet5.5 review27e97a72 (15turns,65857ms,exit0) required two documentation amendments. Lead f9c8adc pins outer-frame/logical coordinate and frame-margin conversion plus cap/degenerate/preferred semantics. Actual Sonnet confirmation ccabe0f2-44a4-4db5-85e4-dc025776f9b4 (6turns,20195ms,exit0) ACCEPTS the corrected contract with no remaining required amendments. Full unedited reports/raw results/prompts/commands are retained. This accepts the contract only; implementation and combined integration remain unverified. The dedicated QtCore/Test fragment parses successfully in contract-only main with implementation absent; this is not a full feature build or runtime pass.

Raw runtime evidence byte check:531 staged members, zero worktree-versus-index byte mismatches, preserving sealed settings/package bytes. See raw-evidence-index-byte-check.json and its exact checker. Git source/test comparison remains identical to7e88979 in the SAFE worktree. Main contains lead contract declarations/wiring only; no SAFE product merge or startup correction implementation was performed.
