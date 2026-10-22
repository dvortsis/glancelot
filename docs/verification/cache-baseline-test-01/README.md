# CACHE-BASELINE-TEST-01

Source `d63cd9d5f36f0134a4b42bcddeaa4b62ddad198f`. The cache test uses the existing private, target-only watcher baseline hook to await registration for cache-hit assertions. Production retains its100ms fallback and mandatory fresh decode after a late baseline; revision/token checks and all existing cache assertions are unchanged. The separate fs_watch late-coalesced-baseline test still exercises production fallback, fresh pixels and newer-open fencing. Test preferences are disposable and their environment override is restored. Root CMake changes were made by the current lead under its integration lock and affect only cache_tests; they also persist raw Qt output.

The previous full Windows attempt at a83e43e failed41/42 in583.07s, solely cache. Explicit Qt-file repetitions reproduced3/10 failures in cached-revisit flags, and focused traces showed extra decode starts from late-baseline restarts. See review/baseline and the CSV integration failed-attempt archive. This is independent of the CSV engine. Initial4fd1ee5 did not compile because I used the wrong private namespace; the failed compile and initial review are retained. Final source corrects fs_watch and isolates preferences, and has a fresh independent approval in review/REVIEW.md; that supersedes the unbuildable checkpoint.

Actual targeted build command is in run-focused.ps1, with configure/build logs. It compiles and links successfully. No existing QVERIFY/QCOMPARE line was removed or changed (source.patch).

Actual repeated command (ten iterations, Qt bin on process PATH):

    ctest --test-dir build-codex-cache-baseline-test-01 -R '^cache$' --output-on-failure -V

Actual output for each of ten repetitions:

```text
100% tests passed out of 1
```

All ten status lines are exit0; all ten raw Qt files report `10 passed, 0 failed, 0 skipped`. Targeted corrected grid repetitions overlapped the cache repetitions and also passed10/10. Complete records are adjacent.

Actual additional command, ten iterations with GLANCELOT_TRACE_FILE pointing at each disposable trace:

    cache_tests.exe bidirectionalNavigation -o <corrected-focused-N.txt>,txt

All ten actual status lines report `exit=0 decodes=5 expected=5`; raw traces contain exactly five decode:begin events for the five navigation steps. This verifies the formerly failing warm-navigation path without fallback restarts. No production timing or decoding behavior changed.

Full Windows/Docker and exact integrated-commit checks are pending at this source-review checkpoint. Their final result is recorded in docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md and the lead closeout on main. These checks do not qualify physical displays.
