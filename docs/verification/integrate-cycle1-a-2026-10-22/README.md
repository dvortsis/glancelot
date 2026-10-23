# Integration cycle1-a, October 22, 2026

Written for: the lead and later reviewers. Records the gate that let EN-05 and EN-09 into `main`.

| Item | Value |
| --- | --- |
| Batch branch | `lead/integrate-cycle1-a` |
| Gated commit | `49fb635` = `f325ef6` + `--no-ff` merges of `claude/en-05-viewer-sources` (`d355edd`) and `claude/en-09-feasibility` (`fb90c18`) |
| Reviews | EN-05 approved (`claude/review-en-05`, `4217209`); EN-09 approved in round 3 (`claude/review-en-09-r3`, `b519278`) |
| Gate | Local tester, both gates in one `gate-lock.ps1 -Run` block, 21:54-22:34 Mountain |
| Windows | `scripts/build.ps1 -BuildDir build-gate-c1a -Test`: 43/43 passed, 775.10 s tests, 1439 s wall, no QTest failures |
| Docker | `docker build --progress=plain`: exit 0, 43/43 passed in the image, 390.41 s tests, 960 s; image 547,658,756 bytes |
| Reruns | none |
| Merge into main | `main` gained only documentation commits since `f325ef6` (`445fc26`, `4f29124`, `9ec08c0`); no file under `src`, `ui`, `app`, `include`, `tests`, `cmake`, `scripts`, `CMakeLists.txt` or `Dockerfile` differs between `49fb635` and the merge |

The lock wait took about 57 minutes because other sessions took the lock first each time it was released. The lead's build-slot and pending-gate scripts (`claude/lead-scripts`, in review) remove that starvation.

## Files

- `summary.txt`: SHA, CTest summary lines, times, image size, from the tester.
- `gate-c1a-driver.log`: the whole locked block.
- `windows-ctest-summary.txt`, `docker-ctest-summary.txt`: per-test lines.
- `results/`: the QTest results files from the Windows build directory.
