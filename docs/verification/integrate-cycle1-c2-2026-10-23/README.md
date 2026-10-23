# Integration cycle1-c2, October 23, 2026

Written for: the lead and later reviewers. Records the gates that let EN-04 and STACK-F054-COVERAGE part 1 into `main`.

| Item | Value |
| --- | --- |
| Batch branch | `lead/integrate-cycle1-c2` |
| Commits | `8943463` = `main` + merges of `claude/en-04-harness-wiring` (`8c6701e`) and `claude/stack-f054-coverage` (`7281aa8`) + lead label commit; `febd0ed` = `8943463` + `COPY translations translations` in the Dockerfile build stage |
| Reviews | EN-04 `claude/review-en-04-r2` (`b90f7c4`, follow-ups `8ee0895`); F054 part 1 `claude/review-stack-f054` (`a7fa4ee`) |
| Lead decisions | F054's `cameraSummary` returns empty when there is no camera (the HUD shows a translated label; `copySummary` omits the line); `translation_coverage_ui` labelled `ui process` and built by the monitor harness |
| Pulled from the batch | STACK-F049-PRESETS: on the earlier batch `6762b51` the fail-fast run failed `translation_coverage_ui` because F049's new Settings strings are not translatable; F049 lands with F054 part 2 |
| Windows | `8943463`: fail-fast 7/7, then `scripts/build.ps1 -BuildDir build-gate-c1c2 -Test` 45/45, 745.75 s tests, no reruns |
| Docker, first run | `8943463`: exit 1, 44/45; `TranslationCoverageUiTests::initTestCase()` could not load `/src/translations/glancelot_en_XA.ts` because the build stage did not copy `translations/` (`docker-gate-8943463-fail.log`) |
| Docker, fixed | `febd0ed`: exit 0, 45/45 in the build stage, 398.26 s tests, 984 s; `translation_coverage_ui` 9/9 (`docker-build-stage-translation_coverage_ui-results.txt`); image 548,222,540 bytes |
| Why Windows was not rerun | `git diff --stat 8943463 febd0ed` is the one Dockerfile line, which the Windows build and tests do not read |
| Merge into main | `main` gained only documentation commits since the batch base; no file under `src`, `ui`, `app`, `include`, `tests`, `cmake`, `scripts`, `translations`, `CMakeLists.txt`, `Dockerfile` or `.github` differs between `febd0ed` and the merge |

The new FIFO gate queue served the tester in order behind review sessions; waits were 7-11 minutes.

## Files

Tester logs (build, fail-fast, Windows, both Docker runs, drivers), `summary.txt`, CTest `LastTest.log`, and QTest results files under `results/`.
