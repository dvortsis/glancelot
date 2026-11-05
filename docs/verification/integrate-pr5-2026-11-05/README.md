# Integration of PR 5, November 5, 2026

Written for: the lead and later reviewers. Records the gate that let PHASE2-CONTRACTS into `main` (PR 5, merge `ec1202d`).

| Item | Value |
| --- | --- |
| Gated commit | `bb1ae59` = reviewed `3288407` + merge of `origin/main` (no conflicts, no product code from `main`) |
| Reviews | Round 3 APPROVED: `docs/verification/PHASE2-CONTRACTS-REVIEW-R3.md`; rounds 1 and 2 beside it |
| Windows | `scripts/build.ps1 -BuildDir build-gate-pr5 -Test`: 52/52, 1016.20 s tests, 2048 s wall, no reruns |
| Docker, first run | exit 1 after 1 s: Docker Desktop was not running (`docker-daemon-down.log`) |
| Docker, rerun | exit 0, 51/51 in the build stage, 1558 s, on the same commit, started with `docker desktop start` |
| Image size | `docker image inspect` now reports 2,820,942,340 bytes, against 548,284,730 at cycle1-h. Docker Desktop's state changed between these runs and PR 5 adds no dependency. The cause is not investigated |
| Hosted `viewer` | Failed twice at `bb1ae59` on different tests (`fs_watch`; then `fs_watch_budget`, `recovery_controller`, `tabs_recovery`, `monitor_customization_ui`). Handled as known red on PR 5, with PR 10 as the fix |

## Files

`summary.txt`, `sha.txt`, `driver.log`, `driver-docker.log`, `windows.log`, `docker.log`, `docker-daemon-down.log`. Results files were not kept.
