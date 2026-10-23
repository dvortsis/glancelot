# Integration cycle1-b, October 22-23, 2026

Written for: the lead and later reviewers. Records the gate that let EN-02, EN-03, EN-06 and EN-08 into `main`.

| Item | Value |
| --- | --- |
| Batch branch | `lead/integrate-cycle1-b` |
| Gated commit | `13b78ff` = `9e62aaa` + `--no-ff` merges of `claude/en-02-linux-bootstrap` (`dffc799`), `claude/en-08-docs` (`7d152cc`), `claude/en-06-routing` (`0b977db`), `claude/en-03-ctest-labels` (`dd64c55`), plus lead commit `0d02914` (EN-06 test target on `GLANCELOT_VIEWER_SOURCES`; hosted job installs `qtimageformats` with a new cache key) |
| Lead resolutions in the EN-03 merge | `docs/TESTING.md` keeps both sides; `command_routing` labelled `ui process`; capability page, `docs/DOCKER.md` and `docs/TESTING.md` describe Docker with `qtimageformats` |
| Reviews | EN-02 `claude/review-en-02` `d29a0c9`; EN-03 `claude/review-en-03-r3` `b77997e`; EN-06 `claude/review-en-06` `9241d03`; EN-08 `claude/review-en-08-r3` `312d2cf` |
| Gate | Local tester, both gates in one `gate-lock.ps1 -Run` block, 22:58-23:44 Mountain; lock wait about 11 minutes |
| Windows | `scripts/build.ps1 -BuildDir build-gate-c1b -Test`: 44/44 passed, 734.15 s tests, 1480 s wall; `ctest --print-labels` lists budget, clipboard, fast, native, process, ui |
| Docker | `docker build --progress=plain`: exit 0, 44/44 in the build stage, 391.46 s tests, 1265 s; runtime plugin check (`qwebp`, `qtiff`, `qtga`, no unresolved libraries) passed; `testMultipageTiffNavigation` PASS in the build stage (`docker-build-stage-animation-results.txt`); image 548,197,595 bytes, 538,839 more than cycle1-a |
| Reruns | none |
| Merge into main | `main` gained only documentation commits since `9e62aaa`; no file under `src`, `ui`, `app`, `include`, `tests`, `cmake`, `scripts`, `CMakeLists.txt`, `Dockerfile` or `.github` differs between `13b78ff` and the merge |

The queue rows EN-08 proposed for the eleven merged stack features, and the `feature`/`commit`/`integrated_evidence` fields for COPY-MOVE-01, FORMAT-02 and RENAME-01, were applied with this integration (strict parse: 85 tasks, no duplicates).

## Files

- `summary.txt`, `gate-c1b-driver.log`, `build-gate-c1b-labels.log`: tester summary, the locked block, CTest labels.
- `windows-ctest-summary.txt`, `docker-ctest-summary.txt`: per-test lines.
- `docker-build-stage-animation-results.txt`: QTest results of `animation_tests` inside the Docker build stage.
- `results/`: QTest results files from the Windows build directory.
