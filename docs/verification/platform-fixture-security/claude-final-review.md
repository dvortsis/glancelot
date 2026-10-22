# PLATFORM-FIXTURE-SECURITY-01 final independent evidence review (Claude PLATFORM lane)

Candidate: `c289dfdb205bd5f278dc398e532c12db78e665e6` (worktree `claude-review-platform-fixture-final`, branch
`claude/review-platform-fixture-final`). Reviewer: Claude, independent of the implementing Codex lane.
Earlier source review: `752758c` (retained as `platform-fixture-security/claude-source-review.md`).

## Methods actually used

Read-only only. I read AGENTS.md, `docs/PLATFORM-FIXTURE-SECURITY-01-PLAN.md`, `RUNTIME.md`, the prior source review, the
Windows preflight, first-run, full-platform-case and Defender records, and the Linux first-failure, isolated and retry logs
(first-failure and isolated logs were searched for the totals, failing assertion and per-test lines, not read line by line).
I ran read-only `git` commands (rev-parse, diff, diff --stat, log, status) and file searches.
**I did not build or execute any binary, test, Docker image, shell payload or retired fixture, and I ran no test myself.**
All pass/fail statements below are the supplied logs' contents, not my observations. The logs cannot be independently
re-verified from here: they are text files produced by the implementing lane.

## Git facts (verified)

- `HEAD` is `c289dfd`. `git diff fce5096 c289dfd -- src include tests cmake CMakeLists.txt app scripts Dockerfile` is
  empty. So source is identical to the `fce5096` I reviewed, and `c289dfd` adds only the 10 evidence files under
  `docs/verification/platform-fixture-security/`.
- `git diff 7cfcca1 HEAD -- src include app cmake CMakeLists.txt` is empty: production code, public headers and root
  CMake/fixture CMake are unchanged from the frozen baseline. The only `src`-adjacent changes since baseline are
  `tests/*` (removed `native_platform_fixture.hpp` and `platform_launcher_stub.cpp`; added `platform_argv_capture.cpp`,
  `platform_fixture_capture.hpp`; edited the test adapter and `platform_integration_tests.cpp`), docs and
  WORK-QUEUE. The test-file diff touches `Q_OS_WIN` guards only by removing one `#ifdef Q_OS_WIN` / `#endif` pair, not by
  editing the Linux test body or the production Linux branch. I did not re-read the Linux test body line by line.
- Worktree status was clean before this report.

## Claim-by-claim check

| Claim | Artifact | Result |
|---|---|---|
| Helper identity before first run | `windows-first-run-preflight.txt`: Unicode path (`native argv ☂ 🧪`), length 23552, SHA256 A63F8AA8…49F, candidate `fce5096` | Substantiated. Closes my earlier evidence gap 1 for presence and identity. |
| First native run 26/26, 0 skips, 1190ms | `windows-first-run.txt` | Substantiated: 26 PASS lines, `Totals: 26 passed, 0 failed, 0 skipped`, 1190ms. The exit code 0 is asserted in RUNTIME.md only; the file has no exit-code record. |
| Full Windows 29/29, 288.03s | `windows-full-pass.txt` | Substantiated: `100% tests passed out of 29`, `Total Test time (real) = 288.03 sec`, folder_grid 176.07s, platform_integration 0.71s. The helper target appears in the build (`glancelot_test_argv_capture` MOC step). |
| Windows platform 26/26, 0 skips, 684ms | `windows-full-platform-cases.txt` | Substantiated, identical case list to the first run. |
| Defender: only old inactive threat | three `*-defender*` / preflight records | Substantiated as read: ThreatID 2147738096 `Behavior:Win32/DefenseEvasion.A!ml`, IsActive False, DidThreatExecute False, ActionSuccess True, resource is the retired `platform_launcher_stub.exe` in the codex-fs-watch-01 worktree. No new detection in the three snapshots. The records are identical before and after, so they show no change, not a clean machine, and no conclusion about false positives. |
| Linux first run 28/29, grid failure | `linux-full-first-failure.txt` | Substantiated: folder_grid `***Failed 104.17 sec`, `FAIL! GridUiTests::reviewCompactCellsSnapToWholeRows() 'live <= columns * 8' returned FALSE (w 300 h 420 delegates 9 columns 1)` at `grid_ui_tests.cpp(279)`, `Totals: 73 passed, 1 failed, 4 skipped`; platform_integration passed 0.48s; `97% tests passed, 1 tests failed out of 29`. The 4 skips are in the grid suite, not the platform suite. |
| Isolated unchanged grid method 3/3 | `linux-grid-isolated.txt` | Substantiated: initTestCase, the method and cleanup PASS, `3 passed, 0 skipped`, 4083ms. Note the image: it is the same compiled image per RUNTIME.md; I cannot confirm that from the log alone (the log has no image digest). |
| Complete unchanged retry 29/29, 158.32s | `linux-full-retry-pass.txt` | Substantiated: `100% tests passed, 0 tests failed out of 29`, `Total Test time (real) = 158.32 sec`; build steps 1-9 and `[build 10/11]` compile are `CACHED`, `[build 11/11]` CTest reran; the runtime image was exported as `glancelot-browser:main`. |
| Initial failure retained, no UI/test-limit change | git diff | Substantiated: no change to `ui/`, `src/`, or `grid_ui_tests.cpp` (the test is not in the diff). |

## Findings

No blocking findings.

1. **Medium, process (gate, not code): the Linux full gate passed only on a retry.** The same source failed 1/29, then
   passed 29/29. The failing test is an inherited grid test unrelated to this change (zero overlap with the diff). The
   differing variable between runs is timing (104s then 115s for folder_grid; other lanes' processes were also running).
   A flake explanation is plausible but not proven: the isolated pass is consistent with it, not a root-cause analysis.
   The integration lead must record this honestly (the first failure remains in the record) and should either run the
   Docker gate on the integrated commit and expect the same flake risk, or track the grid test separately. I would not
   block this task on it, because it is a recorded flake outside this task's files.
2. **Low, evidence precision.** RUNTIME.md states "process exit 0" for the first Windows run, but no exit-code record is
   in the artifacts. The 26/26 totals line makes this very likely; it is not independently documented.
3. **Low, evidence precision.** `windows-compile.txt` is still the partial log with inconsistent step counters. RUNTIME.md
   already says it is not treated as a clean build transcript; the full-suite log now supplies the helper build step.
4. **Low, Defender interpretation.** The before/after Defender records are identical and reference only the retired
   fixture. They show no new detection for the replacement helper during those two windows. They do not show that
   Defender scanned the new helper, and there is no clean-machine, false-positive or general antivirus conclusion. RUNTIME.md
   claims no such conclusion, which is accurate.
5. **Low, environmental.** A separate lane's `monitor_output_tests` process was present, so no desktop or host isolation is
   claimed (stated in RUNTIME.md; I agree).
6. **Info, carried over from the source review.** Items 2-8 of my source review (path encoding, timing, console,
   COM precondition, partial residue, root-validation breadth, untested null-PIDL branch) are unchanged. The first
   run resolved the three that were runtime-dependent: Unicode path/encoding, no-console helper, and the initial COM
   state all passed (26/26 including `initTestCase`). Timing: the whole platform suite ran in 0.68-1.19s, far under the
   30s CTest limit. The untested null-PIDL branch remains untested.

## Native versus controlled evidence

- **Native/real (per the logs):** a real process launched by Qt `QProcess`; module, argv, CWD and console state as reported
  by the helper; Qt quoting round trips for empty, quoted, trailing-backslash, metacharacter, maximum-count and
  maximum-budget payloads; helper rejection of bad nonce, root, size, count, missing and pre-existing output; real PIDL
  construction and `SHGetNameFromIDList` on generated files; real COM apartment init/uninit balance; real filesystem
  validation; Qt `findExecutable` of the helper by its friendly name in an explicit directory.
- **Controlled/test data:** the adapter's `wt.exe` lookup result, launcher success/failure sequencing, the captured
  `SHOpenFolderAndSelectItems` call, and the launcher names `explorer.exe`, `wt.exe` and `powershell.exe`, which are
  recorded and never executed.
- **Unverified:** installed Explorer, Windows Terminal and PowerShell launch behavior, detached-process and UI behavior,
  and the real Windows executable search order. Windows platform behavior is verified only to the extent above. Linux
  desktop launchers were not part of this task.

## Remaining integration conditions

All of these remain with the lead/integrator:

1. Hold `integration_lock` in `docs/WORK-QUEUE.yaml`, merge current `main` into the reviewed branch, and rerun
   `scripts/build.ps1 -Test` and `docker build -t glancelot-browser:main .` **on the integrated commit**. Evidence on
   `c289dfd` does not transfer to a merge result.
2. Require that production `src/` and `include/` and the Linux platform branch remain identical to baseline `7cfcca1`
   after merging `main`. At this candidate they are.
3. Record the Linux first failure alongside the passing retry; do not delete the failure.
4. Do not restore, allow-list, rebuild or run `platform_launcher_stub` or `native_platform_fixture.hpp`.
5. The security hold stays in place. Only the lead may release it after the integrated gates and Defender status
   inspection. This review does not release it.

## Disposition

**Acceptable for integration, subject to the integrated-commit gates above.** The source is byte-identical to the
reviewed `fce5096`, production and the Linux branch are unchanged from baseline, and the supplied logs substantiate each
numeric claim in the handoff, with the minor precision gaps listed. No source changes are required. This is not a
release of the security hold, and it is not a claim that the Windows OS launchers or an antivirus product were verified.
