# Monitor correction evidence

Candidate intake: `agent/monitor-output` at `0e32d16`, source `27bc47c` atop `ae1b7e4`.
Codex correction branch: `codex/monitor-output-r2`; final source candidate at this evidence stage: `9b03718`.
No product merge into main occurred.

## Source progression

- `6a08c3e`: per-output viewports, bounded SDR color atlas and output command handling. Full Windows passed 33/33 in 494.36 seconds. Docker context transfer was canceled before compilation; exit 130.
- `a0ba770`: retained reference viewport metadata while its image is absent, color lifecycle notifications and native display probe. Independent probes had failed the missing-image zoom and close-notification cases; both failures are preserved.
- `c84ea83`, `51fb9c6`: fixed original fit-mode regressions exposed by the preceding change. Targeted suite passed 37 cases; two native-only checks skipped. A subsequent full run was explicitly stopped when independent review required further source changes. Partial output is retained and unqualified.
- `9b03718`: independent-review fixes for first-visit display defaults, mode-aware texture budgets and no navigation-history retention while presentation is closed. Added targeted regressions; improved periodic profile refresh, resource reservation and keyboard recovery.

## Reviews

`CLAUDE-REVIEW.md` and `claude-review.json` retain the unedited actual Sonnet 5.5 review, session `842f8174-64ec-4369-bba1-a482257ae935`, exit 0. It read the nine required UI skills and requested three source corrections. Those are implemented at `9b03718`; confirmation is recorded separately when available. Codex cannot approve its own corrections.

## Native and targeted evidence

- `windows-sonnet-fixes-cases.txt`: 40 passed, 0 failed, 2 native-only skips, 2108 ms, exit 0.
- `windows-native-reviewed-fixes.txt`: 4 passed, 0 failed, 782 ms, exit 0. A generated Display-P3 shader patch matched Qt's reference conversion. Per-output zoom/source centers restored across the two attached native displays at DPR 1 and 2; first-visit defaults were checked before changing the second display. The Windows profile status reported `P27h-20`.
- `windows-dpr2-proof.txt`: controlled `QT_SCALE_FACTOR=2` actual-size arithmetic. This is separate from the native attached-display probe.
- `windows-edge-original.txt`: failed independent-reference restoration and missing color-status notification before their corrections.
- `windows-edge-corrected-cases.txt`, `windows-edge-final-cases.txt`: original fit regressions exposed during correction. `windows-restored-fit-cases.txt` records their successful correction.

These are generated-pixel and viewport-state proofs. They do not establish physical ICC calibration, HDR, input-to-photon latency, suspend, GPU loss, or physical hotplug recovery. Linux currently uses an explicit sRGB fallback when no native output profile adapter is available.

## Full gates

All full gates use the main checkout's machine lock, in one PowerShell process:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-output-r2 -Run {
    & ./scripts/build.ps1 -BuildDir build-codex-monitor-output-r2 -Test
    docker build --progress=plain -t glancelot-browser:codex-monitor-output-r2 .
}
```

The actual orchestration records Windows exceptions separately and explicitly checks Docker's native exit status. Native proof environment overrides are removed before the full run.

`windows-reviewed-full.txt` and `docker-reviewed-full.txt`, with their corresponding status files, are the fresh gates on `9b03718`. A missing status file means pending, not passed. Earlier outputs remain under their original names; no failed or canceled run is replaced.

Raw outputs and unedited reports are marked binary to preserve exact bytes. The targeted build runner only substitutes `--target monitor_output_tests` in the existing build initialization; no CMake or production script is modified for it.

Source confirmation completed: actual Sonnet5.5 ACCEPT, session9b6ceafa-6ec9-4f52-8be0-f2bc7c5b3670, exit0. See unedited CLAUDE-FOLLOWUP.md and claude-followup.json. It read all nine required UI skills and confirmed required findings1–3 closed. Full gates remain a separate qualification.

Fresh Windows gate on9b03718 completed:33/33,450.91seconds,exit0. Actual windows-reviewed-full.txt and windows-reviewed-full-status.txt. Docker on the same source is now compiling; no Linux pass claimed until its status completes.

## Completed final gates

Both fresh gates completed on9b03718: Windows33/33,450.91seconds,exit0; LinuxDocker33/33,232.74seconds,exit0. The Docker image built successfully. Actual full outputs and native exit-status files above are final. The machine gate lock is free. These are candidate checks; no main source integration occurred.
