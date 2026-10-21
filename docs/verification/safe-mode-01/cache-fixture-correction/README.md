# SAFE qualification follow-up: cache-path test fixtures

Current status: exact796ab411 passes complete Windows37/37 and Docker37/37 gates. Fresh raw logs and35-command process proofs are retained below. Independent review of the six-line fixture change is pending authorization to send the prepared broader watcher payload to Claude. No integration approval or task-done claim is made.

Product source remains exactly03bd32687b0e47cc71306d7667927eeb896785e3. Only tests/fs_watch_tests.cpp changes: the two tests that require a genuine unchanged-source cache hit now use the same5000ms private baseline wait as the existing IPC Fixture. Assertions and real source/hash checks remain unchanged. Native registration remains enabled; the real decoder and generated media remain in use. No production header/implementation, root CMake, queue, renderer flag,250ms GUI budget or CTest timeout changes.

The local control/adverse probe in ../sort-status-corrections demonstrated that production's intentional100ms fallback can start a revision worker, then replace it with a fresh decode when registration arrives late; the unchanged source is still validated but a cache hit is not guaranteed on that path. The fixture-only correction makes the two cache/admission tests exercise the cache path they assert. It does not establish that late baseline delivery caused the prior full-run failures. Existing controllerLateCoalescedBaselineDecodesOnce fresh-pixels and newer-open-wins cases continue to use production's100ms behavior and must pass unchanged.

Prior results remain explicit:03bd326 Linux37/37 passed, Windows36/37 then35/37 failed; the quiet Windows watcher suite82/82 and isolated budget3/3 passed. No failed gate is waived. The budget timeout is separate and is not claimed fixed by this fixture change. The focused and full results below qualify the current test/source snapshot; independent cross-lane review is still required. The user has been asked to authorize the broader read-only Claude watcher payload after automatic approval review rejected that export scope; no payload was sent. Integration remains the lead's responsibility.

The focused build succeeded. The complete watcher target passed82/82, zero failures/skips, in84.44s (CTest1/1,84.53s). windows-build.txt/windows-focused.txt/windows-fs-watch-results.txt retain actual output. Both original positive-cache assertions passed; controllerLateCoalescedBaselineDecodesOnce(fresh-pixels) and (newer-open-wins) passed with the unchanged production100ms fallback. No assertion or timeout was removed or changed. Actual focused verification used the main-checkout gate lock around `ctest --test-dir build-codex-safe-mode-01 --output-on-failure -R '^fs_watch$'`. This target pass is not a complete Windows gate or independent approval.

Pinned qualification candidate:796ab411776068a6a7b8d40f26b64930818fd279. Fixture source commit is bdaa779328a8370c3244b4bb185461b14580177b;796ab411 only adds handoff-local byte-preservation rules for nested sealed evidence. The root .gitattributes is unchanged. A check of56existing sealed evidence members found zero differences between worktree bytes and committed blobs (../sort-status-corrections/git-evidence-byte-check.txt). Product source remains03bd326; full gates now qualify the six-line fixture delta too.

Fresh complete Windows and Docker gates completed on exact796ab411. windows-gate-command.ps1.txt/docker-gate-command.ps1.txt retain actual pinned scripts; both ran through the main checkout's gate-lock in the same PowerShell process. windows-full.txt/linux-docker-full.txt retain actual waits and terminal output. Docker used `docker build -t glancelot-browser:main .`. The broader Claude watcher export still requires the explicit approval already requested; no attempt to rerun that rejected export was made.

## Complete Windows result on796ab411

The fresh locked Windows gate terminated with exit0. Actual output in windows-full.txt:

```text
9/37 Test #9: window_slots ..................... Passed 24.76 sec
10/37 Test #10: folder_grid ..................... Passed 197.68 sec
19/37 Test #19: fs_watch ........................ Passed 82.34 sec
20/37 Test #20: fs_watch_budget ................. Passed 108.40 sec
34/37 Test #34: recovery ........................ Passed 14.00 sec
35/37 Test #35: recovery_startup ................ Passed 0.26 sec
36/37 Test #36: recovery_controller ............. Passed 12.78 sec
37/37 Test #37: recovery_process ................ Passed 28.31 sec
100% tests passed out of 37
Total Test time (real) = 558.04 sec
windows_gate_exit=0
```

windows-full-fs-watch-results.txt retains82/82 zero failures/skips, including both positive-cache tests and both unchanged late-baseline safety rows. windows-full-recovery*-results.txt retain137+61+29 recovery passes. windows-full-fs-watch-budget-results.txt retains the unchanged real100k-entry timing/identity assertions. Historical cache failures and the prior budget timeout remain under ../sort-status-corrections; this pass is not a claim that their full-run causes were proven.

windows-process retains the actual35-command passing proof (run-22ipv31d). The two safe GUI runs each recorded23 decoded-image swaps, with1250/1203ms first-image swap,24render callbacks and zero invalidations, using observed D3D11/backend3, CPU device5/Microsoft Basic Render Driver. Screenshots and RGB verifier results are included. Viewer SHA256 is AEEF747304755D69C7482C83B2D15F3177D172FE226874441CD71181D8364272, identical to the earlier03bd326 executable; product code is unchanged. These are software-profile swap observations, not physical GPU/DPI or input-to-photon qualification. Power-loss and sanitizer evidence remain unverified.

Docker acquired the shared lock after Windows released it and completed on this exact796ab411 candidate, as recorded below. Independent cross-lane review of the fixture is still required. No integration is performed or implied.

## Complete Docker result on796ab411

The locked Docker gate also terminated with exit0 and built glancelot-browser:main. Actual output in linux-docker-full.txt:

```text
19/37 Test #19: fs_watch ........................ Passed 69.03 sec
20/37 Test #20: fs_watch_budget ................. Passed 21.65 sec
34/37 Test #34: recovery ........................ Passed 11.00 sec
35/37 Test #35: recovery_startup ................. Passed 0.04 sec
36/37 Test #36: recovery_controller ............. Passed 11.38 sec
37/37 Test #37: recovery_process ................ Passed 20.92 sec
100% tests passed, 0 tests failed out of 37
Total Test time (real) = 273.13 sec
docker_gate_exit=0
```

Image manifest-list digest: sha256:402c388a32b4e252ce3fc04902fa2636d923fe79025db11962a21eca6ce6d903. Both required complete candidate gates now pass on exact796ab411. linux-evidence-command.ps1.txt/linux-evidence-export.txt record a separate locked cached-stage export, completed with exit0, retaining fresh raw logs/process proof. Its compile/CTest steps explicitly report CACHED; it is not counted as another executed test pass. The temporary evidence container was removed. The six-line fixture correction still needs independent cross-lane review, and the broader Claude payload has not been sent because approval is pending. Candidate gate passes do not constitute integration approval or task completion.

linux-raw retains137+61+29 recovery passes,83watcher passes and3budget passes, all zero failures/skips, plus the full CTest LastTest.log. recovery-process/run-l7h39jyf records35commands and passed=true. The safe GUI runs recorded17/19 decoded-image swaps,739/407ms first-image swaps,18/20render callbacks, zero invalidations and observed backend2/OpenGL with Mesa llvmpipe software renderer detection. Raw RGB verifier results/screenshots are retained. These are software-profile observations; physical GPU/DPI, input-to-photon, power-loss and sanitizer evidence remains unverified.
