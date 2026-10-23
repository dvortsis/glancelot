# Gate triage, October 22, 2026

Written for: the lead and whoever implements EN-11. Records why the first two runs of the self-hosted CI gate failed and what a run outside the runner showed.

| Run | Commit | Where | Result | Failing tests |
| --- | --- | --- | --- | --- |
| [39870526245](https://github.com/dvortsis/glancelot/actions/runs/39870526245) | c4e0829 | self-hosted runner | 40/43, 995.09 s | folder_tree, folder_grid, fs_watch (no results files kept) |
| same run, hosted job | c4e0829 | GitHub windows-2022 | 43/43, 672.09 s | none |
| [39874250713](https://github.com/dvortsis/glancelot/actions/runs/39874250713) (Docker skipped, triage only) | f033070 | self-hosted runner | 40/43, 959.61 s | folder_tree :427, folder_grid :947 and :958, fs_watch :298 and :1023 |
| local tester | afeac1a | this PC, outside the runner, gate lock held | 42/43, 760.36 s | folder_tree :430; passes 29/29 when rerun alone |

The code under test did not change between the last green local gate (ec05e42) and c4e0829 (`git diff --stat ec05e42 c4e0829 -- src ui app tests cmake CMakeLists.txt include scripts` is empty). The runner runs in the interactive desktop session (session 1).

## Findings

- The failures are timing races that load makes likely, and the runner makes them likelier; they are not a broken machine.
- `fs_watch` under the runner logged `generated PNG atomic commit failed 10 "Access is denied."`: a `QSaveFile` rename over a PNG that another handle holds without delete sharing.
- `folder_tree` fails at :427 or :430 depending on timing: a late first file-watcher scan bumps the controller generation after the 100 ms wait, and the navigation finish handler drops the in-flight folder-open result, so no status is set.
- QTest prints nothing into CTest's LastTest.log on this machine; only the `-o <file>,txt` results files carry failure detail. Since f033070 every QTest suite writes one and both gate jobs upload them.

Follow-up packet: EN-11 in docs/WORK-QUEUE.yaml.

## Files

- `runner-39874250713-*-results.txt`: QTest results files from the triage run.
- `local-afeac1a-folder_tree-results.txt`: the local gate's folder_tree results (before the standalone rerun).
- `local-afeac1a-ctest-summary.txt`: CTest summary lines of the local gate.
