# SAFE-MODE-01 correction continuation

Candidate source: 7bffd68a4c004ce565032d384ebbd0077afa33b9 on codex/safe-mode-01. No additional product edit was needed before the final focused checks; git diff against the candidate for src/include/ui/tests/app/scripts/root CMake/cmake modules was empty. Earlier complete 37/37 runs qualify the previous candidate only.

Fresh focused build uses the candidate scripts/build.ps1 environment/configuration with its build line restricted in an ignored runner to targets recovery_tests, recovery_startup_tests and recovery_controller_tests. Command: `& ./.cache/continued-corrections-build.ps1 -BuildDir build-codex-safe-mode-01`. Actual output: windows-focused-build.txt.

With project-local Qt bin and Python Scripts on PATH:
`ctest --test-dir build-codex-safe-mode-01 -R '^(recovery|recovery_startup|recovery_controller)$' --output-on-failure`

Actual output (windows-focused-ctest.txt):
```
1/3 Test #34: recovery ......................... Passed 14.56 sec
2/3 Test #35: recovery_startup ................. Passed 0.28 sec
3/3 Test #36: recovery_controller .............. Passed 7.74 sec
100% tests passed out of 3
Total Test time (real) = 22.77 sec
```

Raw retained QtTest logs: recovery-results.txt (137 passed,0 failed,0 skipped,14455ms), recovery-startup-results.txt (61 passed,0 failed,0 skipped,184ms), recovery-controller-results.txt (17 passed,0 failed,0 skipped,7618ms). They include the blocked own-checkpoint/preference persistence case, foreign shortcut conflict after deferral, retry exhaustion with responsive event loop and owner deletion, malformed/future recovery read-only resume, live Session restore sequencing, and exit-contention backoff.

An initial attempt to invoke CTest omitted the project-local Python Scripts directory from PATH and did not execute CTest; the command was corrected before the fresh run above. No stale per-case result is used as proof.

Full Windows command, queued with actual waiting/running output in windows-full.txt:
`& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/safe-mode-01 -Run { & ./scripts/build.ps1 -BuildDir build-codex-safe-mode-01 -Test }`

Full Linux command, queued with actual output in linux-docker-full.txt:
`& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/safe-mode-01 -Run { docker build --progress=plain -t glancelot-browser:safe-mode-01-review-7bffd68 .; if ($LASTEXITCODE -ne 0) { throw 'SAFE-MODE Linux Docker gate failed' } }`

Both use one PowerShell process for lock and command and cannot overlap a full gate held by another branch. Pending gates are not passes. Actual process verifier artifacts and full-gate outcomes must be added after completion.

The existing Claude review checkout is clean and pinned at the same7bffd68. A read-only Sonnet5.5 follow-up invocation was attempted but automatic approval review rejected process creation, stating that no trusted user message explicitly authorized sending the source/instructions/generated evidence to the external Claude destination. No review request was executed or payload transferred. Prior authorization is recorded in AgentMemory but was not accepted by auto-review. Explicit user approval is pending through the async question; local qualification continues independently. No review approval, integration or packet-done claim is made.
