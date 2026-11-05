# STACK-CONFIRM round 5: review of the STACK-CONFIRM-FIX round 5 changes (PR 3)

**Verdict: APPROVED**

Nothing blocks. On Windows the GUI thread can no longer wait on anything a dead decoder holds, on any path, exit included, because no cross-process lock is left. Memory is bounded and returns to the baseline. This branch is not slower than its base: `input_ui`, `folder_grid` and `fs_watch_budget` match the base on a quiet machine and on hosted CI. The ctest timeouts in the handoff came from machine load.

**Is Phase 1's confirmation complete? Yes, once the tester's full gate and `docker build` pass on the merge head.** Neither has run on this code, and this review did not run the full suite (section 7).

Two test races are worth fixing before the gate, because each can fail a gate that takes about 45 minutes. Neither is a product defect, and both fixes are one line, shown working here:

- **R5-N1:** `input_ui` `precedenceMatrixForTheSameKey` predates this PR. It failed at the tip on a quiet machine in this review.
- **R5-N2:** `closingTheViewerMidTransferLeavesNoMemoryBehind` is new in this PR and failed in 2 of 54 loaded runs.

**This approval and the `fresh-review` status cover `99af568` only.** The lead has two paths:

- **Gate `99af568` as it is.** R5-N2 can then fail the gate with no rerun allowance, because `stack_confirm_ui` is not on the budget-test list in AGENTS.md. Its failures here were all under load, and the gate runs alone, so the risk is lower there but not measured. R5-N1 can also fail it, but `input_ui` is on that list, so one rerun is allowed.
- **Take the one-line R5-N2 fix first.** That makes a new head with changed test code, which needs a short re-review of those lines before `fresh-review` can be posted on it.

R5-N1's file is outside this packet's grants, so that fix is the lead's call either way.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review subagent on the lead's Windows PC. It implemented none of this and wrote no earlier review. |
| Target | PR 3, head `99af568` (`origin/claude/stack-confirm-fix`), `git diff 9c0c919 99af568`. The head did not move during the review. |
| Inputs | `STACK-CONFIRM-REVIEW-R4.md`, handoff section "Round 5", queue row STACK-CONFIRM, `docs/contracts/` on `origin/main`. |
| Environment | Windows 11 Pro 10.0.26200, MSVC 2022 Release, Qt 6.8.3. Separate clean builds of the tip and the base (`.worktrees/review-stack-confirm-r5`, `.worktrees/review-stack-confirm-r5-base`) through `build-slot.ps1`. Linux: WSL2, Qt 6.8.3 `gcc_64`, `~/gl-cs` at `99af568`. |
| Evidence | `docs/verification/STACK-CONFIRM-R5/`. Probes were applied only while running and then reverted. Their source is `review-probes.diff.txt` (the Windows hostile-worker and latency probes and the closing-test change). The Linux probes are the same kind of test, and their output is in `linux-probe1.txt` to `linux-probe3.txt`. No product code is on the review branch. |
| ui-design-skills | Not read. This round has no visual change (slideshow timing and one stop condition). |

## 1. Status of the round 4 items

| Item | Status | Evidence |
| --- | --- | --- |
| R4-B1 no viewer-level test | **fixed** | Reproduced here: with `TransferSegment` back on `QSharedMemory`, `aDecoderDying...` dies in 33 s with `The GUI thread has been blocked for 30000 ms: the viewer is frozen.` (`mutations.txt`) |
| R4-B2 memory never given back | **fixed** | The reaper is deleted. 948 more real-decoder steps side by side here: 0 undecoded, 0 mapped after each pass, handles 356 to 354, private bytes 185 to 187 MiB, max GUI gap 931 ms (`step4.txt`). The implementer's 3,792 steps are in the handoff |
| R4-N1 guard not pinned | **fixed** | Guard removed: `slideshowWaitsForASlowFolderStep` fails both rows, `Compared lists have different sizes` |
| R4-N2 short photo after a slow step | **fixed** | Restart removed: `e-1.png was on screen for 277 ms at a 1000 ms pace` |
| R4-N3 manual open during its own step | **fixed** | Handler removed: all 3 rows fail. Signal choice in section 4 |
| R4-N4 reaper tests' absolute counts | **gone** | The tests went with the reaper |
| R4-N5 short deadlines, wrong reason | **fixed** | In `tests/stack_confirm_ui_tests.cpp`: no plain `QTRY_*` left, no `*_WITH_TIMEOUT` under 10000 ms, `sleep-ok` reasons true, stray comment moved. `pr_guard` from `origin/main`: `code=True, 0 error(s)` |
| R4-N6 `Math.min(target, item.x)` not pinned | **unchanged, info** | Harmless guard, no action |

## 2. Item 1: the freeze and the data handoff

**Can the GUI thread wait on a dead worker? Not on Windows, on any path.** `QSharedMemory` and `QSystemSemaphore` are gone from `src/` and `app/`. The only remaining use is `tests/fs_watch_decoder_fixture.cpp`, on purpose. Window close on Windows only kills and returns. The other `waitFor*` calls in `src/app` wait on thread pools or on thumbnail workers, which do not use this transfer.

**The handoff is correct without a lock.** The worker writes, says `decoded`, and the viewer validates and copies, then says `ok`.

| Case | Result |
| --- | --- |
| Worker dies mid-write (probe: half written, then `_Exit`) | Viewer never reads. "Decoder failed; the previous valid image is preserved." |
| Truncated mapping | Not possible. A page-file section cannot shrink, and commit is charged at `create`, so a shortage fails there with a message rather than as a fault on read |
| Stale mapping or name reuse | Key is a new UUID. `create` refuses `ERROR_ALREADY_EXISTS` (mutation-tested by the implementer). The kernel object dies with its last handle |
| Worker claims `decoded` without writing (probe) | Shows fresh zero pages (`#00000000`), never an earlier photo's pixels. The same exposure existed before; a worker can always write wrong pixels |
| Handles | Created non-inheritable (`nullptr` attributes), so a later worker does not inherit them. `map` closes on failure, the destructor closes everything, and a viewer crash leaves nothing because the kernel closes it |
| Size | `VirtualQuery` `RegionSize` stops at the allocation, so it is the view's page-rounded size |

**Hostile or broken worker: no viewer test covered this, so I added a review-only probe** that drives the real viewer path with a lying fixture (`hostile.txt`; `Totals: 15 passed, 0 failed`). Every row kept the previous image, and a healthy decode worked afterwards with no mapping left.

| The worker sends | Viewer status |
| --- | --- |
| `decoded` before any `probe` | Decoder returned invalid image geometry. |
| `probe` with wrong `bytes`, 32768 x 32768, negative sizes, or a second `probe` | Decoder requested an invalid pixel allocation. |
| `decoded` with double width, 1000 x height, wrong stride, width 65535, 32768 x 32768, half width | Decoder returned invalid image geometry. |

The prefetch path has the same checks at `viewer_controller.cpp:730` and `:757`. I read those and did not probe them. Limits and admission are unchanged: 256 MiB, the headroom check and `admitIoJob` are not in the diff.

**Is `aDecoderDyingMidTransferNeitherBlocksNorKeepsMemory` fair? Yes.** It runs the real viewer and window, and its fixture takes the old transport's lock and dies at several points. With MC1 it freezes as claimed, and I reproduced that. `closingTheViewerMidTransferLeavesNoMemoryBehind`, which the handoff could not show failing on Windows, also fails under MC1: `Test function timed out` at the 90 s limit I set, which fills that gap. The `9c0c919` reaper failure (`19 segments mapped at once`) I read from the handoff's evidence and did not re-run. It is a fixture rather than the real decoder, so the rate runs remain the proof with real decoders.

The GUI-thread cost of a teardown at the largest admitted size, 256 MiB with every page touched, was 131 to 167 ms under load here (58 to 72 ms in the handoff). That is only at the 256 MiB limit, and it waits for no other process.

## 3. Item 2: Linux

**Is `retireIoProcess` holding the segment needed? Yes.** Qt removes a System V segment only if the last detach finds nobody attached, so letting go while a killed worker is still exiting leaves it for good. The handoff shows 13 and 3 left at `fdcb2a9`. It costs nothing on the GUI thread, because nothing waits.

**Is the `~Private` wait bounded? Yes, and it can't block use.** It runs only when the viewer is destroyed, on the current two workers, at most 1 s each, so 2 s at worst, and only for a worker that SIGKILL can't end (stuck in the kernel on a dead share). Measured, closing with a live transfer took 86 to 114 ms for the whole harness teardown, and no segment was left in 5 of 5 runs. It is needed: the handoff shows 1 left with the wait removed.

**Can a segment still be orphaned? Yes, on three paths.** The task counts a leak as blocking, so here is why these don't block. This PR adds no new way to orphan a segment. Each path below also orphans one without the hold, where the segment is let go at the kill, and that base behaviour leaves far more: the handoff's run at `fdcb2a9`, which released at retire, left 13 and 3 in two rows of 18 steps. That run is not `main` or `9c0c919` themselves. I infer they behave the same because their viewers release the segment at the same moment; I did not run the Linux probes on them. The rate is low: 0 orphans in the 2,686 Linux rate-run steps (1,896 here, 790 in the handoff).

| Path | Reproduced | Result |
| --- | --- | --- |
| A worker retired just before the process exits, so nothing delivers `finished` and `~Private` doesn't know it | **Yes, 5 of 5** when closed in the same event-loop turn. **0 of 5** with 20 ms of event loop first | One segment per retired worker until reboot (`linux-probe2.txt`) |
| The viewer itself crashes or is killed mid-transfer | **Yes** | Segment stays with `nattch 0` after its worker goes. Predates this PR (`linux-probe2.txt`) |
| A retired worker stuck in the kernel at exit, or the 1 s wait expires | By reading only | Same outcome |

This is R5-N3. I found no close path in the app that retires a worker just before destruction: `onClosing` and `aboutToQuit` don't reach `cancel`. So path 1 needs a retire within a few ms of closing, such as a slideshow tick.

**The one 64 MiB orphan is not explained.** It was size 4096 x 4096 x 4, so `liveLargeDecodeCancellationKeepsExplicitOpen`. Path 1 does not fit well, because in the same process a later test's event loop releases the hold; probe rows with a later event loop show the count falling back. Ruled out or not repeated here:

- **A fork child holding the mapping:** 0 of 300 with a last detach straight after `QProcess::start` (`QT_FEATURE_forkfd_pidfd` is on in this Qt, and the result fits `start` not returning before exec).
- **Under load:** no orphan in 24 runs of that function, 4 side by side, or in 1,896 rate-run steps at paces 0, 20 and 40 ms (`linux-probe3.txt`).

What it does show is the design's weak point: Qt makes one attempt, at the last detach. Anything still attached at that moment makes it permanent. A remaining candidate is something briefly holding the dying worker's memory, which I could not test.

**Docker: it can't outlast the container.** With Docker's default private IPC namespace, the kernel destroys a namespace's System V objects when its last process ends (`ipc_namespaces(7)`). Each `docker build` `RUN` and each container start gets a new one. `compose.yaml` sets no `ipc:` mode, and the viewer's exit ends the container (`docker/start.py`), so an orphan goes with it. Until then it counts against the container's memory. `shm_size` applies only to `/dev/shm`, which these segments don't use. I reasoned this from the documentation and did not run Docker; the tester's `docker build` runs these tests.

No System V semaphores or `qipc` files in `/tmp` were left after the runs that counted them (`linux-probe1.txt`, `linux-probe3.txt`).

## 4. R4-N3: is `viewCommand` sound?

**Sound, and a dedicated signal is worth one header line when the header next opens. It does not need to hold this PR.**

- The id is not in the command registry, so it can't collide. In `handleViewCommand` it ends the loupe, which a folder change ends anyway, then falls off the `else if` chain with no effect (`ui/Main.qml:751`).
- The three emit sites are correct. `open` skips the navigation's own call and same-folder opens, so `slideshowKeepsGoingAcrossAFolderBoundary` is unaffected. `FolderTree.qml` calls `openFolder` only from user actions, so a tree following the slideshow can't stop it.
- The tests that spy on `viewCommand` still pass.
- The cost is a navigation notice on a signal documented by use as a command broadcast, and internal `navigateDirectory` callers emit it too. Those are harmless as the handoff reasons, because a reconcile that moves folders during a step ends a slideshow anyway.

## 5. Timing: is this branch slower? No

**Quiet machine.** Gate lock held throughout, nothing else in a slot. Load samples are in `load-input.txt` and `load-grid.txt`; one sample shows a `docker` process, so another session's container or client was running then. Whole `ctest` or direct runs, in the order run:

| Suite | Base | Tip | Note |
| --- | --- | --- | --- |
| `input_ui`, first ctest after building | 104.62 s, passed | 119.19 s, **failed** `precedenceMatrixForTheSameKey` (R5-N1) | Includes a 5 s `QTRY` wait in the failing tip run |
| `input_ui`, direct | 80.6 s | 82.5 s | Per-function differences all under 1 s |
| `input_ui`, ctest | 84.87 s | 92.46 s | Tip ran first in this pair |
| `folder_grid`, ctest | 236.31 s | 235.55 s | |
| `fs_watch_budget`, ctest | 113.73 s | 114.70 s | |
| Open-to-ready, real decoders, 2000 x 1500, 80 opens, 3 runs each | median 247.8, 247.8, 243.8 ms | median 247.6, 247.6, 245.5 ms | Same probe source in both trees |

**Hosted CI, `windows-2022` `viewer` job.** No load from this PC. Seconds; the tip has two runs and the base three:

| Suite | Tip `99af568` | Base `9c0c919` |
| --- | --- | --- |
| `input_ui` | 71.3, 67.5 | 74.3, 69.2, 72.4 |
| `folder_grid` | 183.3, 189.9 | 209.6, 199.6, 203.2 |
| `fs_watch_budget` | 94.0, 89.0 | 100.1, 85.2, 100.1 |
| `fs_watch` | 82.8, 82.9 | 83.5, 83.3, 82.8 |
| `stack_confirm_ui` | 131.3, 126.1 | 91.1, 89.5, 90.6 |
| Whole suite | 953.1, 936.1 | 965.2, 915.8, 959.9 |

The locally measured `input_ui` difference of 2 to 8 s is not visible on hosted CI, where the tip is if anything faster, so I read it as run-to-run noise. Only `stack_confirm_ui` grew, by its own new tests.

**Why the handoff saw 229 s and timeouts: load.** Under build slots here, `stack_confirm_ui` took 413.59 s against 171.5 s quiet, a factor of 2.4. `input_ui` at 229 s against about 90 s is the same factor. I did not run the base under the same load, because the quiet and hosted comparisons already answer the question. `folder_grid` (limit 400 s), `input_ui` (240 s) and `fs_watch_budget` (180 s) have small margins for a factor of 2.4, so they will time out under three busy slots on any commit. That argues for keeping them out of busy slots rather than for raising limits; the gate runs alone.

**`stack_confirm_ui`'s TIMEOUT of 600 s is safe:** 126 to 171 s when quiet, 413.59 s and the handoff's 384 s under slots. It is slower than it needs to be, though not by much: `slideshowWaitsForASlowFolderStep(5 s pace)` is 48 s of 171 s, and the 1 s row covers the same logic in 14 s (R5-N4).

**`precedenceMatrixForTheSameKey`: not from this change.** Same load, same six-at-a-time campaign, alternating:

| | Failures |
| --- | --- |
| Tip | 20 of 30 |
| Base `9c0c919` | 19 of 30 |
| Tip with the wait in R5-N1 | 0 of 30 |

## 6. Findings

None blocking.

**R5-N1 (LOW-MEDIUM, gate risk, predates this PR): `precedenceMatrixForTheSameKey` rotates before the photo has landed.**

- Where: `tests/input_ui_tests.cpp:121`. The test waits for `currentIndex` to change, not for the decode. When the decode lands, `setImage` resets `m_rotation` (`src/render/image_canvas.cpp:280`), so the rotation is lost at `:139` or `:143`.
- This is the test's fault, not the product's: a rotation of the previous photo shouldn't survive a new one.
- It failed on the first ctest at the tip on a quiet machine, so the tester's gate can hit it. `input_ui` is a budget test, so a rerun is allowed by the rule.
- The mechanism rests on the 20 of 30 against 0 of 30 result. A separate probe that delayed every decoder's `decoded` message by 3 s passed once (`mutations.txt`, row DELAY). I did not trace why; a prefetch may have cached the photo first, or the decode may have landed outside the rotation checks. So a fixed delay is not a way to reproduce this.
- Fix, shown above (0 of 30): after that `QTRY_COMPARE`, add `QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.loading() && !h.viewer.navigating(), 15000);`. The file is outside this packet's grants, so the lead decides where it goes.

**R5-N2 (LOW-MEDIUM, gate risk, new in this PR): `closingTheViewerMidTransferLeavesNoMemoryBehind` checks the mapping once.**

- Where: `tests/stack_confirm_ui_tests.cpp:409`, `QVERIFY(mappedTransfers(bytes) > 0)`.
- Failed in 1 of 6 and then 1 of 48 under load. The diagnostic in the failing run: `markers 1, loading 1, status Opening p1.png…`. So the viewer had restarted the same photo's decode (most likely a late file event on fresh media), and the first worker's segment was gone and the next not yet created.
- Fix: `QTRY_VERIFY_WITH_TIMEOUT(mappedTransfers(bytes) > 0, 15000);`. 0 of 48 with it (`closing.txt`), a small sample for a rate this low.
- `stack_confirm_ui` has no rerun allowance, so this is the one finding that can fail the gate outright.

**R5-N3 (LOW, Linux only, no new path from this PR): a worker retired before the viewer closes can still leave a segment.**

- Where: `src/app/viewer_controller.cpp:497`. `~Private` waits only for `worker` and `prefetchWorker`.
- Direct fix for path 1: keep a `QList<QPointer<QProcess>>` of retired processes in `Private`, and wait for all of them there with one 1 s budget in total.
- Fix for all paths except a crash, and for the unexplained orphan: have the viewer mark the segment for removal itself when it lets go (`IPC_RMID`), so the kernel removes it at the last detach whoever that is. The handoff rejected this because it relies on Qt's private key scheme; it would also make the hold and the wait unnecessary.
- Fix for a crash too: `memfd_create`, which has the Windows lifetime and no `/dev/shm` limit, but changes how the worker attaches.
- I would not hold Phase 1 for any of these. Record it for the lead with the Linux platform work. It is unverified on physical Linux.

**R5-N4 (LOW): `stack_confirm_ui` could lose 28 % of its time.** `tests/stack_confirm_ui_tests.cpp:494`: the 5 s pace row exercises the same guard as the 1 s row. Reasonable to drop or to shorten to 300 folders.

**R5-N5 (INFO, for the RESOURCE-LEDGER packet): the release order is the opposite of the contract's.** `src/app/io_job_lease.hpp:34` resets the lease and then the segment, and the decoder `finished` handler does the same. RESOURCE-LEDGER says the reservation goes "after the buffer is really unmapped and closed". Today nothing runs between the two, so nothing can observe it. The packet that adds the Transfer reservation should swap them.

## 7. Whole STACK-CONFIRM and the frozen contracts

**Confirmation.** The round 1 and round 2 tables in `STACK-CONFIRM-REVIEW-R4.md` section 5 still hold. All R3 and R4 items are fixed or recorded in the queue row. The queue row's `acceptance` names `STACK-CONFIRM-REVIEW.md APPROVED`, while reviews are numbered by round, so the lead should point it at this document.

**Contracts: no conflict.** `RESOURCE-LEDGER` line 58 and `DECODE-TIER` line 130 say "the class is whatever PR 3 lands", and `COMPARE-PANES` line 63 asks for one buffer per pane. `TransferSegment` gives one per instance, each with a new key. Two wording points for the lead, neither a defect:

- `RESOURCE-LEDGER` line 61 says the GUI thread "never takes a cross-process lock on a transfer buffer and never blocks on one". On Linux `QSharedMemory`'s create and detach take its semaphore on the GUI thread, and `~Private` waits on the worker at close. That semaphore is released by the kernel on the holder's death, and with the hold no live worker can be holding it at detach, so nothing can get stuck. `transfer_segment.hpp:14`'s "no lock" is true of the class's API, and its lines 11-12 say so. The rule's intent holds; its letter holds on Windows only.
- `RESOURCE-LEDGER` line 60: see R5-N5.

**Merge.** `git merge-tree origin/main 99af568` has no conflict at `7209262`. `main` does not use the reaper at all, so the deletion can't break it.

**Grant check.** `git diff --name-only 9c0c919 99af568` matches the handoff's list. No queue edit, nothing under `include/glancelot/`.

**Unverified.** Full `scripts/build.ps1 -Test` and `docker build` (the tester's gate). `reference_pin_native` and native D3D captures. Physical Linux. A worker truly stuck in the kernel (read, not run). The prefetch path's hostile-header checks (read, not probed).

## 8. Test output

Windows, targeted, `ctest`, under a build slot with other sessions in the other slots (`targeted-tip.txt`):

```
1/9 Test  #7: viewer_acceptance ................   Passed   27.63 sec
2/9 Test  #8: thumbnails .......................   Passed   11.08 sec
3/9 Test #23: fs_watch .........................   Passed  129.30 sec
4/9 Test #31: cache ............................   Passed   13.37 sec
5/9 Test #34: animation ........................   Passed    0.68 sec
6/9 Test #44: tabs_recovery ....................   Passed   99.91 sec
7/9 Test #47: translation_coverage_ui ..........   Passed   55.32 sec
8/9 Test #49: command_routing ..................   Passed   27.30 sec
9/9 Test #52: stack_confirm_ui .................   Passed  413.59 sec
100% tests passed out of 9
```

Under the gate lock (`timing-input.txt`, `timing-grid.txt`), at the tip:

```
input_ui ......... ***Failed 119.19 sec   (first run: precedenceMatrixForTheSameKey, R5-N1)
input_ui .........   direct: 33 passed, 0 failed, 1 skipped
input_ui .........   Passed   92.46 sec
folder_grid ......   Passed  235.55 sec   (74 passed, 0 failed, 4 skipped)
fs_watch_budget ..   Passed  114.70 sec
stack_confirm_ui .   direct: 30 passed, 0 failed, 1 skipped, 171455ms
```

Both results are recorded as the budget-test rule asks: `input_ui` failed once and then passed twice, and its cause is R5-N1.

`aDecoderDying...` plus `closingTheViewer...` plus the three segment tests, repeated under a slot: 5 of 6 runs passed, and run 2 failed as in R5-N2 (`step4.txt`). PR CI on `99af568`: `ci-ok`, `core` and `viewer` all pass. Linux at `99af568` (`linux-probe1.txt`, `linux-probe3.txt`): the dying and closing tests passed 2 of 2, and the rate runs had 0 undecoded and 0 segments left. The implementer's full Linux suite for `stack_confirm_ui`, `fs_watch` and `fs_watch_budget` at `16a251e` is in the handoff; I did not repeat it.
