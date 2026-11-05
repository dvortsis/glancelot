# Integration of PR 3, November 5, 2026

Written for: the lead and later reviewers. Records the gate that let STACK-CONFIRM-FIX into `main` (PR 3, merge `7c0da34`), which closes Phase 1.

| Item | Value |
| --- | --- |
| Gated commit | `da2280a` |
| Reviews | Round 5 APPROVED at `99af568`: `docs/verification/STACK-CONFIRM-REVIEW-R5.md`, evidence in `docs/verification/STACK-CONFIRM-R5/`. Rounds 1 to 4 beside it |
| Changes after review | `7b3a8c4` merges `main`, `da2280a` applies the review's own R5-N1 and R5-N2 test fixes |
| Windows | `scripts/build.ps1 -BuildDir ... -Test`: 53/53, 1205.12 s tests, 2289 s wall, no reruns |
| Docker | exit 0, 52/52 in the build stage, 1502 s |

## Image size

`docker image inspect` reports 2,821,031,803 bytes, about the same as PR 5's 2,820,942,340 and well above cycle1-h's 548,284,730. This is probably a change in what is measured, not growth, but that is not proven:

- `docker history` shows the Qt layer alone at 1.46 GB, so 548 MB cannot have been an uncompressed total.
- Docker now runs the containerd image store (`driver-type io.containerd.snapshotter.v1`), and `docker image ls` lists disk usage and content size separately.

The gate removed its image afterwards, so the content size was not read. Settled at the next gate: `docs/verification/integrate-pr9-2026-11-05/README.md`.

## Files

`summary.txt`, `sha.txt`, `driver.log`, `windows.log`, `docker.log`, `docker-history.txt`.
