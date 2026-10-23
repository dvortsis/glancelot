# Integration cycle1-d, October 23, 2026

Written for: the lead and later reviewers. Records the gate that let EN-11 into `main`, and the first of the three consecutive full gates EN-11's acceptance asks for.

| Item | Value |
| --- | --- |
| Batch branch | `lead/integrate-cycle1-d` |
| Gated commit | `5908284` = `b9dc0ec` + merge of `claude/en-11-gate-stability` (`392c711`) |
| Reviews | approved at `99dbe9f` and `305cda9` (named in the merge commit) |
| Gate | Local tester, fresh build directory `build-gate-c1d`; fail-fast run, then Windows and Docker in one `gate-lock.ps1 -Run` block (acquired 06:08:11 after a 688 s wait for build slots to drain) |
| Fail-fast | 9/9 in 345.13 s: viewer_acceptance, folder_tree, folder_grid, fs_watch, late_baseline, fs_watch_ui, cache, metadata, animation |
| `ctest -N` | 46 tests; labels fast 28, ui 7, budget 7, native 9, process 26, clipboard 3 |
| Windows | `scripts/build.ps1 -BuildDir build-gate-c1d -Test`: 46/46, 719.06 s tests, 792 s wall; folder_tree 9.43 s (29 passed), folder_grid 205.65 s (74 passed, 4 skipped: review-capture tests that need `GLANCELOT_REVIEW_CAPTURE`), fs_watch 87.95 s (82 passed), late_baseline 1.67 s |
| Docker | exit 0, 46/46 in the build stage, 453.47 s tests, 1104 s; image 548,224,002 bytes (+1,462 over cycle1-c2) |
| Reruns | none |
| "Access is denied" | Two QWARN lines from `QFileSystemWatcher` (`FindNextChangeNotification failed`) inside passing tests (acceptance `folderJumpsAfterExactEmptyVisits`, folder_grid `clipboardOperationsRegisteredAndFunctional`); two informational lines in late_baseline, which records replace-over-reader outcomes without asserting them. None in fs_watch or folder_tree, which failed on this message before EN-11. |
| Generated PNG retries | 0 "generated PNG ... retrying" or failure lines in any results file |
| Merge into main | `main` gained only documentation commits since `b9dc0ec`; no file under `src`, `ui`, `app`, `include`, `tests`, `cmake`, `scripts`, `translations`, `CMakeLists.txt`, `Dockerfile` or `.github` differs between `5908284` and the merge |

EN-11 stays in `integration` until two more consecutive full gates pass with folder_tree, folder_grid and fs_watch; the next integration gates count.

## Files

- `summary.txt`, `gate-c1d-driver.log`: tester summary and the locked block.
- `build-gate-c1d-build.log`, `build-gate-c1d-failfast.log`, `build-gate-c1d-windows.log`, `build-gate-c1d-docker.log`, `LastTest.log`: build and test logs.
- `results/`: QTest results files from the Windows full gate. The fail-fast run wrote to the same files, so only its log is kept.
