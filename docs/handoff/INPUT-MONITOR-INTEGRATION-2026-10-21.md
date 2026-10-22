# INPUT and monitor integration, October 21, 2026

Both packets are integrated locally into main and marked done. No push or unrelated feature integration was performed. Independent reviews used actual Claude Sonnet 5.5, with all nine native UI skills. All full gates used the main checkout gate-lock script; no full gates overlapped.

## INPUT-02

Accepted synchronized source `83100c4`; no-ff merge `d59369014574658893458ea403a08161d986b67e`. Main synchronization exposed a test binding collision and grid search/chord interaction; both are corrected and independently accepted. The new test first reproduced the overly broad routing regression, then passed the narrowed chord rule. Root/busy/target parity and clipboard semantics are preserved.

Actual final command output:

```text
Windows: 100% tests passed out of 34
Total Test time (real) = 493.56 sec
exit=0
Linux Docker: 100% tests passed, 0 tests failed out of 34
Total Test time (real) = 297.09 sec
exit=0
```

Entire merge tree equals qualified `83100c4`. Checks at the exact integration commit:3/3 in 92.18 seconds, exit 0 (window_slots, input_ui, platform_integration). Earlier Windows33/34, interrupted builds/Docker and rejected broad routing remain archived.

[Actual commands, full output and independent reviews](../verification/integration-2026-10-21/input/README.md).

## MONITOR-OUTPUT-01

Accepted source `9b03718` synchronized as `b128be9`; no-ff merge `8e8871a2832478cf652c764409994c261f01184e`. The only sync conflict retained three additive ImageCanvas includes. Eighteen Git blobs prove accepted monitor code/tests and main INPUT/watcher content unchanged. Sonnet accepted the combined canvas/controller source.

Actual final command output:

```text
Windows: 100% tests passed out of 34
Total Test time (real) = 659.04 sec
exit=0
Linux Docker: 100% tests passed, 0 tests failed out of 34
Total Test time (real) = 350.52 sec
exit=0
```

Entire merge tree equals qualified `b128be9`. Checks at the exact integration commit:3/3 in 91.35 seconds, exit 0 (input_ui, viewport, monitor_output). Separate native Windows generated-color and display-state probes:4 passed, 0 failed, 0 skipped in 892 ms, exit 0. Saved physical zoom/source centers restored across attached DPR 1/2 displays; shader patch matched Qt reference; profile status P27h-20.

[Actual commands, full output, source-equivalence checks and independent review](../verification/integration-2026-10-21/monitor/README.md).

Full gates above qualified the candidates; integrated commits have exact tree-equivalence proofs plus focused checks. No second full suite on either merge commit is claimed. Physical ICC calibration, HDR, hotplug/suspend/GPU-loss, IME/keyboard layouts and trackpad coverage remain unverified. Non-blocking primary-canvas hold edge cases are retained in the monitor review. Empty root authoring logs o.txt/o2.txt are removed in the evidence closeout; product source is unchanged.

## Next work

Claude must merge integrated main forward through F002, F037, F031, F040, F049, F009, F014, F051 in stack order; the routed Gemini confirmations still apply before those packets qualify. Their implementation worktrees were not edited this round. STARTUP-WINDOW-01 remains ready in the Codex lane and blocks SAFE-MODE-01 integration; neither was merged here.

The evidence closeout releases integration_lock. Check the machine-local gate with scripts/gate-lock.ps1 -Status. All requested source and evidence changes are committed.
