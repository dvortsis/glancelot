# SAFE-MODE-01 pending sort-save correction

The first full Windows gate on61e4752 failed35/37. The exact window_slots failure was reproduced without modifying its existing test: contendedSortReportsSessionOnly requires session-only feedback while a settings writer lock prevents persistence. saveWithRetry returns acceptance for retry, so its return value cannot establish saved state.

The private controller now tracks pending and persisted states separately. Pending sort feedback says "for this session; saving settings is pending"; only a successful completion clears that qualification. Terminal failure retains session-only failure feedback. Sort worker results identify sort completion, and the GUI composes the message using current persistence state rather than a stale message captured before the worker ran. Deferred save completion updates only the sort feedback that still owns the status, preserving newer navigation/error feedback. No public header, root configuration, queue, watcher source, existing assertion or timing budget changes.

New behavioral coverage exercises pending-to-saved transitions with an empty and a loaded navigator, including the asynchronous sort completion, plus preservation of newer feedback. The existing failed-save,200-edit coalescing, foreign-field and recovery tests remain intact.

Actual focused commands:

```powershell
& ./.cache/continued-corrections-build.ps1 -BuildDir build-codex-safe-mode-01
$env:PATH='C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64/bin;C:/src/Github/Glancelot/.tools/python/Scripts;'+$env:PATH
ctest --test-dir build-codex-safe-mode-01 --output-on-failure -R '^(recovery|recovery_startup|recovery_controller)$'
```

The restricted build runner uses the standard MSVC/Qt setup and builds only recovery_tests, recovery_startup_tests and recovery_controller_tests; it does not run a full gate. Actual output in windows-build.txt/windows-focused.txt:

```text
1/3 Test #34: recovery ......................... Passed 15.17 sec
2/3 Test #35: recovery_startup ................. Passed 0.35 sec
3/3 Test #36: recovery_controller .............. Passed 13.17 sec
100% tests passed out of 3
Total Test time (real) = 28.93 sec
```

Raw QtTest results retain137 recovery,61 startup and29 controller passes (227 total), zero failures/skips. Fresh independent Claude source review and full locked Windows/Docker qualification remain required. The earlier fs_watch full-run failure is not explained by its isolated pass; both results remain in ../confirmed-binding-corrections. Physical GPU/DPI, sanitizer and power-loss tests remain unverified. No integration is performed by this lane.

## Independent review of03bd326

The approved read-only Sonnet5.5 follow-up completed successfully (session af4bd929-c510-4972-a730-c2751d36c0fd,12turns,62990ms; actual canonical model claude-sonnet-5-5). CLAUDE-REVIEW-03bd326.md, raw JSON, prompt, stderr and status retain the independent result. The reviewer found the correction sound with no blocking regression; this is not integration approval or a full-gate result. It inspected source only and did not independently run tests or read raw logs.

Recorded residual findings: when a newer message owns status, a terminal sort-save failure preserves that message and currently has no separate warning/log. The unsaved owned fields remain available for the next edit. There is no new loaded-navigator terminal-failure test or deterministic save-failure-before-sort-completion test. Status ownership uses string equality; an identical newer sort message may be refreshed, but the refreshed persistence state is current. These findings are retained for the lead; no source changes are made during pinned qualification.

One precision correction to the review's test discussion: failedSortIntentIsRetriedWithNextEditAndForeignFieldsPreserved has a meaningful initial QTRY assertion waiting for terminal "could not be saved" feedback while the lock remains held. Its final assertion after an immediate successful second edit is the redundant check noted by earlier reviews. The loaded asynchronous failure permutation remains an uncovered case.

Full qualification is running on exact03bd32687b0e47cc71306d7667927eeb896785e3. windows-gate-command.ps1.txt and docker-gate-command.ps1.txt retain the actual pinned commands; both invoke the main-checkout gate-lock in the same PowerShell process. windows-full.txt/linux-docker-full.txt retain actual output including waits and terminal status. A live or queued run is not a pass.

## Full Windows result

The locked Windows run terminated with exit1:36/37 passed in663.14s. Actual output:

```text
9/37 Test #9: window_slots ..................... Passed 25.73 sec
10/37 Test #10: folder_grid ..................... Passed 212.95 sec
19/37 Test #19: fs_watch ........................ Failed 86.36 sec
20/37 Test #20: fs_watch_budget ................. Passed 164.83 sec
34/37 Test #34: recovery ........................ Passed 13.75 sec
35/37 Test #35: recovery_startup ................ Passed 0.22 sec
36/37 Test #36: recovery_controller ............. Passed 12.97 sec
37/37 Test #37: recovery_process ................ Passed 28.53 sec
97% tests passed, 1 tests failed out of 37
Total Test time (real) = 663.14 sec
windows_gate_exit=1
```

The unchanged window_slots regression now passes. The sole failure is again FSWatchTests::noncurrentSameTupleReplacementInvalidatesCache's initial unchanged-source cache-hit assertion (tests/fs_watch_tests.cpp:684). first-full-fs-watch-results.txt records81passes/1failure/0skips. The full run is not a pass. watcher-source-comparison.txt records byte-identical decode and observation-reconciliation bodies compared with d4f0e70 and no watcher-source/test diff; this narrows the affected code but does not establish the failure's cause or grant a waiver. A quiet full watcher-suite diagnostic is queued behind Docker, with its exact command retained.

windows-process retains the actual35-command executable proof with passed=true. The safe GUI runs recorded28/27 decoded-image swaps,1106/964ms first-image swap,29/28 render callbacks and zero scenegraph invalidations, using observed D3D11/backend3, CPU device5/Microsoft Basic Render Driver. Raw RGB verifier results are included. The tested viewer SHA256 is AEEF747304755D69C7482C83B2D15F3177D172FE226874441CD71181D8364272. These software-profile swap observations are not physical GPU/DPI or input-to-photon qualification. Full-run recovery snapshots use full-* names; original focused snapshots are preserved.

Docker uses the frozen plan's exact `docker build -t glancelot-browser:main .` command. The initial safe-mode-specific-tag wrapper was cancelled while still queued, before running Docker, and its wait log/identity are retained in docker-before-tag-alignment-wait.txt/docker-tag-alignment.txt. The corrected wrapper acquired the lock after Windows released it and is compiling the unchanged pin. There is no Linux result yet.

## Linux result and quiet Windows diagnostic

The exact locked Docker gate completed with exit0 and built glancelot-browser:main. Actual output in linux-docker-full.txt:

```text
19/37 Test #19: fs_watch ........................ Passed 69.44 sec
20/37 Test #20: fs_watch_budget ................. Passed 23.19 sec
34/37 Test #34: recovery ........................ Passed 11.05 sec
35/37 Test #35: recovery_startup ................ Passed 0.04 sec
36/37 Test #36: recovery_controller ............. Passed 11.52 sec
37/37 Test #37: recovery_process ................ Passed 20.47 sec
100% tests passed, 0 tests failed out of 37
Total Test time (real) = 278.39 sec
docker_gate_exit=0
```

The resulting image manifest-list digest is sha256:a2ca65622e6bfa47ac234fec6f55b18e3bc8d2a0f0db1b1259d3f6728c44845c. A separate locked `--target build` cache export retains generated raw Linux artifacts; linux-evidence-command.ps1.txt/linux-evidence-export.txt record this operation. It is not represented as a second executed test gate.

The subsequent locked, unchanged full Windows watcher-suite diagnostic passed82/82, zero skips, in86.62s (CTest1/1,86.65s). quiet-fs-watch-results.txt includes the previously failing unchanged-image cache hit and subsequent changed-image blue pixels/cache miss. No source, assertion, timeout, baseline deadline or renderer flag was changed. This quiet pass does not establish the earlier failures' root cause. It justifies one fresh complete Windows retry on the same03bd326 pin; windows-full-retry.txt retains its actual output. Until that completes successfully, the candidate has no full Windows pass and remains unqualified for integration.

The Linux cached-stage export completed with exit0. linux-raw retains the original successful stage's CTest LastTest.log, recovery/watcher raw QtTest logs and recovery-process/run-bnux246p. That process report has passed=true and35commands. Both safe GUI runs produced18 decoded-image swaps, with805/464ms first-image swap,19 render callbacks each and zero invalidations, using observed backend2/OpenGL and Mesa llvmpipe CPU renderer detection. Raw screenshots/RGB verifier results are retained. These are software-profile observations, not physical GPU, DPI or power-loss evidence. The temporary export container was removed by the successful wrapper; its ID and all build/cache/copy command output are in linux-evidence-export.txt. The unchanged full Windows retry is the remaining live qualification gate.

## Latest checkpoint: Windows retry failed; local diagnostic completed

The unchanged full Windows retry is terminal, exit1:35/37 passed in770.96s. fs_watch failed86.55s at the OTHER positive-cache assertion, controllerCannotReleaseForeignAdmissions (tests/fs_watch_tests.cpp:404); noncurrentSameTupleReplacementInvalidatesCache passed this time. fs_watch_budget timed out. All recovery suites and the35-command executable proof passed. windows-full-retry.txt, retry-full-*-results.txt and retry-windows-process retain actual output/artifacts. No further full retry is launched without a diagnosis. No full gate remains live.

The timed-out budget log records generated100k-file setup135884ms, initial GUI gap62ms, sustained-writer GUI gap101ms, filter GUI gap137ms and publication117ms before timeout. These observations do not erase the timeout or establish where its remaining time went. Its timeout and assertions remain unchanged.

A temporary local private-hook probe compared production's100ms fallback with prompt versus70ms-per-directory delayed baseline reads, using the existing generated decoder fixture and a delayed revision reply. Source bytes were verified unchanged. Prompt baseline produced revisionStarts1/decodeStarts2 and unchangedHit=true. Delayed baseline produced revisionStarts1/decodeStarts5 and unchangedHit=false: a revision worker started1030ms, a fresh decode restarted1170ms at late baseline delivery. The diagnostic positive-hit assertion therefore deliberately exposed the latter difference:3passes/1failure, not a qualification pass. This demonstrates a causal path to an unchanged-source cache miss, but does not prove that path caused the earlier full-run failures. Actual code/output are local-baseline-probe.patch, local-baseline-probe-results.txt and local-baseline-probe-status.txt. The initial temporary probe had a declaration compile error, retained separately; it was corrected before these results.

The runner restored tests/fs_watch_tests.cpp byte-for-byte and forced a rebuild of its original object/executable. Both original positive-cache cases then passed4/4 in1954ms, with every original assertion and deadline unchanged (local-baseline-original-cases.txt/status.txt). No production source was edited. PROPOSED-CACHE-FIXTURE.patch.txt is an UNAPPLIED review proposal: await baseline in the two cache/admission tests using the existing IPC Fixture's5000ms private hook; keep production100ms behavior, every assertion/CTest timeout, and controllerLateCoalescedBaselineDecodesOnce's fresh-pixels/newer-open-wins coverage unchanged. It is not qualified source and needs independent assessment before application.

Automatic approval review rejected exporting this broader watcher source/test/log payload to Claude: the trusted approval covered the isolated SAFE correction payload at7bffd68, not this new cache-failure review scope. The rejected command did not execute and no payload was sent. Local diagnosis continued without bypassing that decision. A new read-only review of the concrete watcher/probe/proposal payload needs explicit user authorization. The current source remains03bd326 and is not qualified for integration because the complete Windows gate has not passed.

The subsequent unchanged isolated fs_watch_budget run passed3/3 (CTest1/1) in145.51s, keeping the180s outer timeout and all250ms GUI assertions. Actual output is windows-budget-quiet.txt/quiet-fs-watch-budget-results.txt; command is quiet-budget-command.ps1.txt. Generated setup100740ms, initial/sustained GUI gaps59/80ms, active-filter gap104ms and maximum publication101ms were recorded. This does not explain or erase the full-run timeout. All gates are terminal. A test-fixture-only follow-up is now tracked in ../cache-fixture-correction; the original03bd326 evidence above remains unchanged. External watcher review is still pending explicit approval.
