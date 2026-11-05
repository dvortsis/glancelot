# STACK-CONFIRM round 4: review of the STACK-CONFIRM-FIX round 3 changes (PR 3)

**Verdict: CHANGES REQUIRED**

The freeze is gone. In every test here, the GUI thread never waited on a lock a dead decoder holds. R3-B1, R3-N1, R3-N2 and R3-N3 are fixed, and all nine targeted suites pass on Windows. `slideshowKeepsGoingAcrossAFolderBoundary` ran 120 times with no hang or failure.

Two findings block approval. Both belong to the R3-B2 fix:

- **R4-B1.** No test pins the fix in the viewer. With the viewer's `TransferPtr` wiring reverted, the five shipped tests I ran on that build (the three reaper tests and two viewer tests) still pass, yet the real viewer freezes again. Round 3 asked for a viewer-level test that fails on the old code.
- **R4-B2.** The freeze was replaced by memory that is never given back, with no limit on total bytes. Real decoders, with no test hook, produced 5 stuck teardowns in 3,792 slideshow-pattern steps. Each one keeps its segment mapped until the app exits. After 16 of them, **every** later transfer leaks, healthy ones included. This breaks the AGENTS.md rule "Bound allocation, queues and decoder lifetime".

**Is Phase 1's confirmation complete? Not yet.** Only R4-B1 and R4-B2 stand in the way. The round 1 items remain fixed or recorded (section 5). The full gate has not run on this head.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review subagent on the lead's Windows PC. It implemented none of STACK-CONFIRM-FIX and wrote no earlier review. |
| Target | PR 3, head `9c0c919` (`origin/claude/stack-confirm-fix`). `git diff b7ca003 9c0c919`, ignoring the merge of `origin/main` `b1d57cd`. Product and test changes are in `34eeab2`. The merge has no code conflict of its own: only `translations/glancelot_en_XA.ts` differs from both parents, by keeping both sides' strings. |
| Inputs | `STACK-CONFIRM-REVIEW-R3.md` and `STACK-CONFIRM-R3/` (`origin/claude/review-stack-confirm-r3` `4f43bab`); handoff "Round 3" section; queue row STACK-CONFIRM (`lead_decisions`, `_r2`, `_r3`); merge rules in `origin/lead/merge-protections:AGENTS.md`. |
| Environment | Windows 11 Pro 10.0.26200, MSVC 2022 Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), Ninja. Detached worktree `.worktrees/review-stack-confirm-r4`, build directory `build-review-stack-confirm-r4` inside it, 746/746 steps through `build-slot.ps1`. Targeted ctest and the stress runs through `gate-lock.ps1`. |
| Machine load | Other sessions were building and gating during the later probes. The rate probes, the external-kill run and the mutation runs overlapped the `phase2-contracts` gate that started at 19:06. If a budget test failed in that gate, weigh this first. The GUI-gap and step-time numbers here include that load. |
| ui-design-skills | Available at `~/.agents/skills/ui-design-skills/skills`. Read `application-interface-design` and `responsive-layout`. I applied them to the 540 px tab strip ("nothing essential should be clipped or unreachable"). Not read: `component-geometry`, `layout-spacing`, `typography-ui`, `motion-ui`, `design-review`. This round adds no new colour, type, radius or motion. |
| Evidence | `docs/verification/STACK-CONFIRM-R4/`. Probes were applied to the test file and worker only while running, then reverted. Their source is `review-probes.diff.txt`. |

## 1. Status of the round 3 items

| Item | Status | Where / evidence |
| --- | --- | --- |
| R3-B1 slideshow treats its own unsettled step as a dead end | **fixed**; one hunk unpinned (R4-N1) | `ui/Main.qml:666`, `:697-700`, `include/glancelot/viewer_controller.hpp` (`navigating`). Section 3 |
| R3-B2 GUI thread blocked in `~QSharedMemory` | **freeze fixed; not approved.** Test requirement unmet (R4-B1), memory unbounded (R4-B2) | `src/app/shared_memory_reaper.hpp`, `src/app/viewer_controller.cpp:157`, `:275`, `:738`, `:1030`. Section 2 |
| R3-N1 540 px tab strip | **fixed** | `ui/Main.qml:1912`. Mutation MQ6 fails at `width 540, active tab 2: its close button is out of view` |
| R3-N2 wheel clamp | **fixed** | `ui/Main.qml:1936`. Mutation MQ4 fails at `contentX 226 -> 210` |
| R3-N3 failure bound pinned | **fixed** | `slideshowPassesAnUndecodableLoneFolderPhoto`. Mutation MQ7 fails it |
| R3-N4 `slideshowStepFrom` stays set | **partly fixed** (R4-N3) | Cleared on settle at `ui/Main.qml:697-700`. The in-flight window remains |
| R3-N5 chips in right-to-left layouts | **unchanged**, info | The app ships `en` and `en_XA` only |

## 2. R3-B2 in depth

### The GUI thread no longer waits on a dead worker

I searched `src/` and `app/` for `QSharedMemory` and `QSystemSemaphore`. Only `viewer_controller.cpp` and `src/worker/main.cpp` use them, plus comments in the new header. The thumbnail service does not use this transfer, so the viewer is the only client to fix.

| GUI-thread call | Can it wait on a dead worker? |
| --- | --- |
| `makeTransfer` / `QSharedMemory(key)` | No. The constructor does not touch the semaphore |
| `create()` (`:739`, `:1031`) | No. It takes the semaphore, but the key is a new UUID that no worker has seen yet |
| `lock()` / `unlock()` before reading pixels | Removed (`:751`, `:1039`) |
| Destroying an attached segment | Moved to a short-lived thread (`shared_memory_reaper.hpp:28-44`) |
| Destroying an unattached segment (`create` failed) | Still on the GUI thread, safe: `isAttached()` is false, so `detach` and `lock` are not reached |

Every teardown goes through the deleter: `cancelLookahead()` `:667`, `cancel()` `:688` (so `fail()` and `~Private()`), the decoder `finished` handler `:1064`, and reassignment at `:738` and `:1030`.

**Is removing the lock safe?** Yes. Neither worker path locks while writing (`src/worker/main.cpp`, no `lock()` call anywhere). So the lock never protected the pixels. The worker finishes writing before it sends `decoded`, and the viewer copies into `owned` before it sends `ok` or resets the segment. The data stays valid for the reader.

**Can the reaper free a segment a live worker writes to?** No harm if it does. On Windows, a worker's mapping stays valid after the viewer closes its own handle. A worker that has not attached yet fails with "Cannot attach", and the viewer has already cancelled it.

**Thread lifetime and exit.** The threads are detached and nothing joins them, so they cannot hang shutdown. The hooked probe closed its harness with 16 threads blocked, and the process still exited with code 0, about 0.7 s after the test functions ended. `QSharedMemory` is deleted on a thread other than the one that created it. It has no parent, timers or connections, and no problem appeared in any run.

### Real proof with a real decoder

The test simulates the dead worker with a holder that never unlocks. **That is a fair proxy for the OS state.** A Windows semaphore has no owner, so a count taken and never returned looks the same whoever took it. It is **not** a fair proxy for the fix, because it never goes through the viewer (R4-B1).

So I added a review-only hook to the real decoder (`review-probes.diff.txt`): `transfer.lock(); std::_Exit(9);`, either after `ok` or straight after `attach`. `_Exit` gives nothing back, as a kill would. Then I drove the real `ViewerController` and window:

| Run | Result |
| --- | --- |
| Tip, die after `ok`, 30 steps | 30/30 photos shown, **max GUI gap 80 ms**, 16 threads blocked, 19 segments leaked (`probe-dead-decoder-tip.txt`) |
| Tip, die after `attach`, 30 steps | "Decoder failed; the previous valid image is preserved" each step, **max GUI gap 32 ms**, 31 more segments leaked. This row ran with the process already at the cap from the row above, so its segments took the leak path: it shows the GUI does not block, not that a reaper thread handles a death at attach |
| Wiring reverted (MC1), die after `ok` | **Frozen in the first step.** `Test function timed out` at 45 s (`probe-dead-decoder-wiring-reverted.txt`) |
| Tip, no hook, real kills: 33 `Kill()` calls at random moments over 79 steps | No hang. 28 photos undecoded, as expected from killing their decoders. Max GUI gap 440 ms (`external-kills.txt`) |
| Tip, no hook, normal stepping, 3,792 steps | 5 stuck teardowns, GUI never blocked, max gap 759 ms (below) |

The last row also settles what round 3 could only infer: **a real decoder really does die holding the semaphore in ordinary use.** At `b7ca003` each of those five would have been a permanent window freeze.

### How often it really happens

Three processes each walked 40 folders of 2 small photos 16 times under "continue" (79 steps per pass). Lookahead runs only near a folder's edge (`viewer_controller.cpp:801-805`), so small folders are the slideshow's own pattern. `stuck` is `pendingSharedMemoryReaps()` at the end, after a settle.

| Pace between steps | Steps | Stuck teardowns | Leaked | File |
| --- | --- | --- | --- | --- |
| 40 ms + up to 20 ms | 1,264 | 0 | 0 | `rate-pace-40ms.txt` |
| 80 ms + up to 20 ms | 1,264 | 1 | 0 | `rate-pace-80ms.txt` |
| 120 ms + up to 20 ms | 1,264 | 4 | 0 | `rate-pace-120ms.txt` |
| **Total** | **3,792** | **5** (1.3 per 1,000) | 0 | |
| One 3,002-photo folder, mid-folder, no lookahead | 1,750 | 0 | 0 | `rate-large-folder-no-lookahead.txt` |

The counts are small, so treat the rates as rough. They agree with round 3's 2 hangs in about 1,000 steps. The trend with pace is suggestive only. I did not run paces of 1 s or more at this volume. The large-folder row shows that most hits need lookahead. So the most likely cause is the prefetch success path, which I read in the code but did not instrument: `process->write("ok\n"); cancelLookahead();` (`:759-760`). `retireIoProcess` closes stdin and kills at once (`io_job_lease.hpp:46`), and the worker's own `~QSharedMemory` at `return 0` (`src/worker/main.cpp:267-268`) takes the semaphore. The kill races that exit.

## 3. R3-B1 in depth

**`navigating` is accurate.** `navigationBusy || navigationSettling || pendingNavigation || intents` (`viewer_controller.cpp:1542`).

- The finished handler (`:1089-1143`) has no early return, so `navigationSettling` always clears, on stale generations too. `q->open()` for the destination runs while settling, and its scan job then sets `navigationBusy`, so the property does not drop in between.
- The handler emits `stateChanged` when it settles, which is the signal the slideshow needs.
- Can it stick true? A fuzz probe mixed 15 operations in random bursts (next, previous, folder steps, open, openFolder, navigateDirectory to a missing folder, cancel, boundary, sort, filter, missing file). **800 rounds: it always fell, the worst in 843 ms** (`navigating-fuzz.txt`).
- By reading only, one narrow path can leave an intent queued: a key pressed while a job runs, followed by `showUnavailable()` (`:232`), which bumps the generation without clearing intents. The stale result then skips the drain. It lasts until the next explicit `open()`, and only applies to a tab whose folder is gone, where a slideshow has already ended. Not a finding. A navigation job stuck on a hung share also keeps it true, so the slideshow waits with its indicator on until the user stops it. That is reasonable.
- The doc comment is accurate: `stateChanged` is emitted when it falls. It is not emitted when it rises, so a QML binding on `navigating` could read stale. Only imperative reads exist today.

**The slideshow.**

- It steps only when settled (`Main.qml:666`).
- It stops at once at a true dead end, with nothing landing later. `slideshowWaitsForASlowFolderStep` waits 1.5 s after the stop and compares the photo. Mutation MQ3 fails `input_ui` `slideshowAdvancesAndEndsCleanly` at `'!flag(h, "slideshowOn")'`.
- The failure bound only runs on a settled step.
- Measured at a 1 s pace across 300 empty folders: `a-2.png` stays on screen for 8.9 s while the step runs, then `c-1`, `c-2` and `c-3` each show for about 1 s, then it stops at `c-3`, the true end (`shown-time-after-slow-step.txt`).

**Spot-checks.** `Main.qml:679` sets `slideshowStepFrom` after `next()`, which is right, because `next()` can emit `stateChanged` synchronously when the queue is full.

## 4. Findings

### Blocking

**R4-B1 (MEDIUM, R3-B2's test requirement unmet): no test covers the fix in the viewer.**

- Where: `tests/stack_confirm_ui_tests.cpp:186`, `:203`, `:224`. All three test `QSharedMemory` and `SharedMemoryReaper` directly. None involves `ViewerController`.
- Evidence, mutation MC1: `viewer_controller.cpp` back to `std::unique_ptr<QSharedMemory>` and `std::make_unique`. Shipped tests with that build: `destroyingASegmentWaitsForTheLockOfADeadWorker` PASS, `theReaperNeverBlocksTheCallerOnAHeldLock` PASS, `blockedTeardownsAreBounded` PASS, `navigatingSpansAStep` PASS, `slideshowKeepsGoingAcrossAFolderBoundary` PASS. The probe with the real decoder: frozen. I ran those five, not the whole suite. None of the others touches this lock deterministically, because the old hang needed a rare race.
- Required by round 3: "Required test: a deterministic one... The viewer must stay responsive... Show that the test fails on the current code." The handoff says plainly that the reaper tests "have no pre-fix counterpart". AGENTS.md's checklist asks for fail-before evidence for every new assertion.
- Fix direction: a viewer-level test with a fixture decoder that answers the probe, attaches, locks and dies, with a `QTRY` on an event-loop heartbeat. The product needs no test hook, because `testHooks->decoderProgram` and `decoderArguments` already exist (`viewer_controller.cpp:775-783`, `:1070-1075`), as does the `tests/fs_watch_decoder_fixture.cpp` pattern. `fs_watch_tests` and `cache_tests` compile with `GLANCELOT_FS_WATCH_TESTING`; `stack_confirm_ui_tests` does not. So it either goes in one of those, which needs a lead grant, or adds the definition in the already granted `cmake/StackConfirmTests.cmake`. `review-probes.diff.txt` shows the sequence. Show it failing with the MC1 mutation.

**R4-B2 (MEDIUM-HIGH, breaks "Bound allocation"): stuck teardowns hold memory until exit, and past the cap every transfer leaks.**

- Where: `src/app/shared_memory_reaper.hpp:34-39`. The header says "decoder lifetime and thread count stay bounded", which is true. It says nothing about bytes.
- What happens:
  - A stuck thread never finishes, so `pendingSharedMemoryReaps()` never falls again and its segment stays mapped. That is up to 16 x 256 MiB = 4 GiB. Neither `IoScheduler` nor the admission check counts it as a held reservation.
  - At the cap, the deleter cannot tell a stuck segment from a healthy one, so **all later segments leak, including those of workers that exited cleanly**. There is no limit on total bytes and no message to the user, only a `qWarning`.
  - It ends only when the app restarts.
- Evidence with the hook (`probe-dead-decoder-tip.txt`), 3000 x 2000 photos (24 MB each): at step 15, 16 threads are blocked; by step 29, 19 more segments have leaked, 35 segments in 30 steps, about 840 MB by arithmetic. Available commit fell by about 1.2 GiB over the same run, but that figure is noisy because other builds were running.
- When a user gets there, extrapolating from section 2's 1.3 to 3.2 stuck teardowns per 1,000 steps: 16 are reached after about 5,000 to 12,000 steps. That is 7 to 17 hours of slideshow at the default 5 s pace, or 1.4 to 3.4 hours at 1 s, in one session. Browsing counts too. Past that point each step leaks at least one decoded image. A 24 MP photo is about 96 MB, so a few minutes of slideshow costs gigabytes. Eventually the existing admission check reports "Insufficient memory headroom" or `create()` fails. I did not run a session that long, so the timing is an estimate.
- This is the normal slideshow path under the default "continue" boundary, not an exotic failure. The trade itself was reasonable for a permanent freeze, but the rule still applies.
- Fix direction (implementer's choice; `lead_decisions_r3` already grants `viewer_controller.cpp`, `io_job_lease.hpp` and decoder-worker lifetime code; none of these was tried here):
  1. Avoid most hits. Stop killing a worker that is about to use the semaphore. After `ok`, close stdin and let it exit, and kill only after a short deadline. Or have the worker skip `detach` after `ok` (exit without running the destructor). For the attach window, a one-line `attached` message would tell the viewer when a kill is safe.
  2. Then bound what is left. At the cap, do not leak healthy segments. For example, a worker that exited normally cannot be holding the semaphore, so its segment can go to an ordinary teardown. Or stop prefetching and tell the user. Round 3's option of a native mapping with no cross-process lock also removes the problem.
- Acceptance: a viewer test for the at-cap behaviour, and a rate run of at least 3,000 steps in the pattern above with 0 stuck teardowns (`probeRealRaceRate` in `review-probes.diff.txt`), or a documented limit on total bytes.

If the lead judges this acceptable for Phase 1, the lead can record a carve-out in the queue row with a named packet, as for earlier items. My verdict stands until then.

### Non-blocking (fix with the above if convenient)

- **R4-N1 (LOW-MEDIUM, test gap): the guard that stops the slideshow stepping during its own step is not pinned.** Mutation MQ1 removes `viewer.navigating` from `ui/Main.qml:666`, and `slideshowWaitsForASlowFolderStep` still passes, because it looks only for `c-2.png` (`tests/stack_confirm_ui_tests.cpp:277`). Without the guard the ticks queue extra steps, and the probe shows the slideshow **skipping photos**: `a-2.png -> c-3.png`, with `c-1` and `c-2` never shown (`mutations-results.txt`). The test should assert that every photo appears, in order.
- **R4-N2 (LOW, predates this round): the photo after a slow step shows for less than the pace.** `slideshowTimer` runs on a fixed grid and is not restarted when a step lands. At a 3 s pace with 120 empty folders, `c-1.png` showed for 1,854 ms (`shown-time-after-slow-step.txt`). A step just shorter than the pace would leave it on screen for almost no time. Restart the timer when the step lands. (SLIDESHOW-01: "per-photo duration".)
- **R4-N3 (LOW, rest of R3-N4): a manual folder change during the slideshow's own step is taken for its own step.** `ui/Main.qml:691`. Probe: 1 s pace, 300 empty folders, `openFolder(z)` while the step runs. Result: directory `z`, `slideshowOn` true (`manual-folder-during-step.txt`). N-2 says opening a folder ends the slideshow. The window is short on a fast disk and longer on a slow one. One option is to compare the destination against what the step could reach, or to have explicit opens clear `slideshowStepFrom` first.
- **R4-N4 (LOW-MEDIUM, test flake): the reaper tests assert absolute counts in a process where the real count can be non-zero.** `tests/stack_confirm_ui_tests.cpp:216`, `:219`, `:221`, `:237`, `:240`. These run after `slideshowKeepsGoingAcrossAFolderBoundary` and `slideshowPassesThroughAOnePhotoFolder`, which both use "continue". If one of those leaves a real stuck thread (section 2), these assertions fail, and `:221` and `:240` would wait out their timeouts. At the measured rates that is on the order of 1 % of suite runs, by estimate. It did not happen in my runs. Compare against a baseline taken at the start of each test.
- **R4-N5 (LOW, merge rule): short deadlines and a wrong reason.**
  - Six added lines use plain `QTRY_*`, a 5 s deadline: `tests/stack_confirm_ui_tests.cpp:246`, `:272`, `:306`, `:483`, `:486`, `:488`. The handoff says deadlines are "at least 10000 ms", and the merge rules ask for at least 10 s. `pr_guard` does not catch plain `QTRY_*`.
  - The `sleep-ok` reason at `:125` talks about closing an inactive tab, which that test does not do.
  - The comment before `frames()` at `:21-22` belongs to `settleSlideshow`.
- **R4-N6 (info): `target = Math.min(target, item.x)` is not pinned** (`ui/Main.qml:1925`, mutation MQ5 passes). It looks unreachable now that `tabCap` makes a row fit the strip. Harmless as a guard.

### Checked without findings

- **Tab strip at 540 px:** by arithmetic the strip is 119 px, `tabCap` 87 px, close button 32 px, so a row fits exactly. `longTabNamesKeepTheActiveTabInView` passes at 1180, 560 and 540 with no sliver at the left edge. I did not take a native capture at 540.
- **Grant check:** `git diff --name-only b7ca003 34eeab2` lists the handoff, three evidence files, `include/glancelot/viewer_controller.hpp` (one property, one getter, a comment), `src/app/shared_memory_reaper.hpp`, `src/app/viewer_controller.cpp`, `tests/stack_confirm_ui_tests.cpp` and `ui/Main.qml`. All are within `lead_decisions_r3`.
- **Merge rules:** `python pr_guard.py --base origin/main --head-ref HEAD`, copied from `origin/lead/merge-protections`: `pr_guard: code=True, 0 error(s)`. All other added fixed waits carry a true `sleep-ok` reason. `git diff b7ca003 34eeab2` adds no queue edit.
- **Linux:** the handoff's claim holds for what I could run. A standalone program built with Qt 6.8.3 `gcc_64` in WSL used the SysV key. A child locked the segment and died by `SIGKILL`, and the parent's destructor returned in 10 ms, 3 of 3 times, leaving nothing in `ipcs` (`linux-shm-probe.txt`). I did **not** build or run the app on Linux.
- **PR CI on `9c0c919`:** `core` and `viewer` pass (`gh pr checks 3`). `viewer` is the Windows job.

## 5. Whole STACK-CONFIRM acceptance

The round 1 table in `STACK-CONFIRM-REVIEW-R2.md` still holds:

| Item | Status |
| --- | --- |
| B-1, G-1, N-1, N-2, N-5 | **fixed**; their tests pass in `stack_confirm_ui` (21/21 functions) |
| G-2 (F031 parts, F053), G-3, G-4, N-3, N-4, N-6, N-7 | **recorded** to E5 or E6 in `lead_decisions` |
| G-5 | **recorded** to CAPABILITY (Phase 2) in `lead_decisions` |
| N-8 | info, no action |
| R2-B1, R2-N1, R2-N2, R2-N3, R2-N5 | **fixed**; tests pass |
| R2-N4, Ctrl+Shift+F as a registry command | **recorded** to E5 in `lead_decisions_r2` |

The queue row's `acceptance` names `docs/verification/STACK-CONFIRM-REVIEW.md APPROVED`, while the reviews are numbered by round. The lead may want to point it at the final round's document.

## 6. Fail-before spot checks

Each hunk was reverted locally and restored afterwards. Results: `mutations-results.txt`.

| Mutation | Test | Result |
| --- | --- | --- |
| MC1 viewer `TransferPtr` wiring reverted | 5 shipped tests | **all PASS: not pinned (R4-B1).** Probe with real decoder: frozen |
| MQ1 `viewer.navigating` removed from the step guard | `slideshowWaitsForASlowFolderStep(1 s pace)` | **PASS: not pinned (R4-N1).** Probe: skips `c-1`, `c-2` |
| MQ2 `!viewer.navigating` removed from the settle check | `slideshowKeepsGoingAcrossAFolderBoundary` | FAIL, `directory().endsWith("/b")` returned FALSE |
| MQ3 dead-end stop removed | `input_ui` `slideshowAdvancesAndEndsCleanly` | FAIL, `'!flag(h, "slideshowOn")'` |
| MQ4 wheel clamp back to no margin | `tabStripWheelRespectsTheMargin` | FAIL, `contentX 226 -> 210` |
| MQ5 `Math.min(target, item.x)` removed | `longTabNamesKeepTheActiveTabInView` | PASS: not pinned (R4-N6) |
| MQ6 `tabCap` back to its old form | `longTabNamesKeepTheActiveTabInView` | FAIL, `width 540, active tab 2: its close button is out of view` |
| MQ7 bound back to `min(5, fileCount)` in every mode | `slideshowPassesAnUndecodableLoneFolderPhoto` | FAIL, `Compared values are not the same` |

## 7. Test output

Targeted suites, Windows, offscreen (CMake test environment), under the gate lock, first run, no reruns (`targeted-ctest.txt`):

`& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review-stack-confirm-r4 -Run { ctest --test-dir ...\build-review-stack-confirm-r4 --output-on-failure -R "stack_confirm_ui|input_ui|folder_grid|tabs_recovery|translation_coverage_ui|viewer_acceptance|animation|thumbnails|cache" }`

```
1/9 Test  #7: viewer_acceptance ................   Passed   17.70 sec
2/9 Test  #8: thumbnails .......................   Passed    7.24 sec
3/9 Test #12: folder_grid ......................   Passed  290.97 sec
4/9 Test #13: input_ui .........................   Passed   93.99 sec
5/9 Test #31: cache ............................   Passed    5.84 sec
6/9 Test #34: animation ........................   Passed    0.53 sec
7/9 Test #44: tabs_recovery ....................   Passed   72.13 sec
8/9 Test #47: translation_coverage_ui ..........   Passed   25.40 sec
9/9 Test #52: stack_confirm_ui .................   Passed  124.62 sec

100% tests passed, 0 tests failed out of 9
Total Test time (real) = 638.47 sec
```

`stack_confirm_ui-results.txt`: `Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 124390ms`.

`slideshowKeepsGoingAcrossAFolderBoundary`, pristine binary, under the gate lock (`stress-slideshowKeepsGoing.txt`):

```
idle sequential: runs=60 hung=0 failed=0 in 00:05:01.8733877
parallel 4x15 (runs,hung,failed per thread): 15,0,0 | 15,0,0 | 15,0,0 | 15,0,0 in 00:01:23.0835339
```

These 120 runs can no longer show the old hang as a hang, because a stuck teardown now blocks only its own thread. So the rate runs in section 2 are the real measure.

## 8. To approve

1. R4-B1: a viewer-level test with a dying fixture decoder, shown failing under mutation MC1.
2. R4-B2: most hits avoided and total bytes bounded, or a lead carve-out with a named packet.
3. Recommended in the same change: R4-N1 (assert every photo), R4-N4 (baseline counts), R4-N2, R4-N3, R4-N5.

A fresh reviewer then re-reviews the decoder teardown and these tests. The tab strip, the failure bound and `navigating` need no further review unless they change.

## Unverified

- Linux and Docker: the app and the new tests were not run on Linux here. Only the standalone semaphore probe above. The Docker gate will cover the tests.
- The full suite was not run by this review. The handoff's run on the round 3 code commit shows 52/52, and no full gate has run on `9c0c919`.
- Rates at paces of 1 s or more were not measured at volume; a long session reaching the cap with real races was not run (the cap behaviour comes from the hooked probe). The cause named in section 2 comes from reading the code, not from instrumenting which path each stuck teardown came from.
- The real application binary was not driven; the probes use the test harness with the real `ViewerController`, QML window and real decoder executable. Screen readers, physical displays and fractional DPI were not checked.
- The fix directions in R4-B1 and R4-B2 were not tried.
- Controller hunks of R3-B1 were read and fuzzed but not mutated. A mutation removing the settle `emit stateChanged` (`viewer_controller.cpp:1142`) was prepared, but its build was still waiting behind the `phase2-contracts` gate after about 25 minutes, so I stopped it and did not run it. Mutation MQ3 pins the dead-end stop from the QML side.
