# Watcher harness phase evidence

Lead packet docs/FS-WATCH-HARNESS-01-PLAN.md promotes the previously deferred watcher timing follow-up after SAFE/startup integration. This is permanent bounded observability, not a timeout fix. The 100000-row workload,99999 native filewrites/removals, selectedimage/identity/writers/filter/rename and every assertion/skip/timeout remain unchanged. No fixture work is excluded from the180s wholeprocess gate.

Source029ad47; synchronized docs-only main into7e660526. The only code change is tests/fs_watch_budget_tests.cpp. source-equivalence.json and verify-source-equivalence.py retain exactbaseline/candidate Gitpins/blobs, normalizedsourcehash and stripping audit: remove only added logs/guard/state assignments andtwo loopbracepairs, then remaining source is identical. This is a leadmechanicalaudit, not independent codeapproval.

Actual independent Sonnet5.5 session54d817cc-6609-42d5-af6a-591318444da5 accepts the source/diff; unedited JSON/result/status retained. It read all diff hunks and actual test/CMake. Advisory: the old timingrisk remains, progresslog overhead is included in setup/lifetime and180s, a historicalfailure phase is stillunproven. No timeoutfix is claimed.

TargetedWindows fs_watch_budget passed118.60seconds,exit0 (QtTest3pass0fail0skip). target-results.txt has17added markers:10phase/6progress/1lifetime, monotonic timestamps. Generationprogress25000/50000/75000 and removalprogress same; finalguard runsafterfixtures. Phaseaudit recorded in target-log-audit.json. Setup~82s and removal~22s dominate this successfulrun; no inference that allhosts fit180s. Source remains fixed.

Actual full gate wrapper full-gates.ps1, from assigned worktree:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/fs-watch-harness-01 -Run {
    & ./scripts/build.ps1 -BuildDir build-codex-fs-watch-harness-01 -Test
    docker build --progress=plain -t glancelot-browser:main .
}
```

The wrapper waited for the user's existing main/test-app.ps1 gate. Its live PowerShell PID66036 and child build were confirmed; it was not interrupted or bypassed. Both subsequent full gates completed successfully; status files record exit=0.

An attempted read of the active QtTest output failed Windows file-sharing; it did not change the test. Completed logs were copied after process completion. No permissions or servicefailure counts as review or testpass.


## Completed integration

Reviewed source029ad478fc08a49b07f3710f9307c3d25572af31 was synchronized with main as candidate7e660526ace5afaa75c3c208955e97ba118c8ee9. Full Windows and Docker gates ran serially in the main gate-lock wrapper shown above. Actual output:

```text
Windows: 100% tests passed out of 39
Total Test time (real) = 669.99 sec
exit=0
Docker: 100% tests passed, 0 tests failed out of 39
Total Test time (real) = 321.45 sec
exit=0
```

Full output: windows-full.txt and docker-full.txt. Windows watcher QtTest:3 passed,0 failed,0 skipped,110605ms; Linux:3 passed,0 failed,0 skipped,22321ms. The Linux build-stage export reused cached gate layers; it is evidence extraction, not an additional fresh full gate. Raw QtTest/CTest results, export commands/status and phase audits are retained.

No-ff merge067244dc651c36f1cefabc3755cccc5c795e5059 has exactly the candidate tree (integrated-tree-equivalence.txt, exit=0). Post-merge command, also under the main gate lock:

```powershell
ctest --test-dir build-codex-fs-watch-harness-01 -R '^fs_watch_budget$' --output-on-failure
```

```text
1/1 Test #22: fs_watch_budget ..................   Passed  108.92 sec
100% tests passed out of 1
Total Test time (real) = 108.93 sec
exit=0
```

integrated-phase-audit.json verifies17 markers, monotonic elapsed time, the fixed phase order and all six progress markers; QtTest3 passed,0 failed,0 skipped. Lifetime108861ms includes fixture destruction. This is a targeted integrated check on the identical merged tree, not a second full suite. No deadline, workload, assertion, fixture lifecycle or production behavior changed. Historical timeout risk remains open; no historical failure phase is established by these passing runs.

All non-README evidence is copied byte for byte from the assigned worktree cache. The Git attributes prevent raw-output newline conversion. Independent review is the retained actual Sonnet5.5 session54d817cc-6609-42d5-af6a-591318444da5; source-equivalence auditing is separately identified as lead verification.

Archive audit:39 original raw evidence files match both source-cache bytes and their staged Git blobs; archive-byte-audit.json records hashes and audit-archive-bytes.py records the audit method (excludes itself and its report).
