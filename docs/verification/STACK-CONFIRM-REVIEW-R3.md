# STACK-CONFIRM round 3: review of the STACK-CONFIRM-FIX round 2 changes

**Verdict: CHANGES REQUIRED**

The round 2 code fixes most of what round 2 asked for. The slideshow now passes through one-photo folders under "continue". "Went nowhere" is judged by path. The failure bound no longer depends on folder size outside "loop". Closing an inactive tab keeps the slideshow. The type chips move with Left and Right. At 560 and 1180 px the tab strip starts on a tab boundary. The seven targeted suites pass on Windows. Six of the seven product-hunk reverts fail the tests that cover them.

Two findings block approval:

- **R3-B1.** One clause of R2-B1 is still open. If the slideshow's own step takes longer than the pace, the slideshow still stops silently. The step then lands a moment later, with the slideshow off.
- **R3-B2.** The intermittent hang in `slideshowKeepsGoingAcrossAFolderBoundary` is a **product hang**, not a teardown race. I reproduced it twice in 250 runs. A captured stack shows the GUI thread blocked forever inside `QSharedMemory::~QSharedMemory`, called from a decoder `readyReadStandardOutput` handler. The cause is older than this packet (`viewer_controller.cpp`). Following the brief's rule, a product hang is blocking.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review subagent on the lead's Windows PC. It implemented none of STACK-CONFIRM-FIX and wrote neither earlier review. |
| Target | `git diff 509f6f9 b7ca003` (`origin/claude/stack-confirm-fix`; round 2 code `632ab4e`, `8e6c3d4`, `607546c`; handoff and evidence `b7ca003`). |
| Inputs | Round 2 review `docs/verification/STACK-CONFIRM-REVIEW-R2.md` (`origin/claude/review-stack-confirm-r2` `6d0bfc4`). Handoff `docs/handoff/STACK-CONFIRM-FIX-HANDOFF.md`, "Round 2" section. Queue row STACK-CONFIRM on `origin/main` (`lead_decisions`, `lead_decisions_r2`: R2-N4 deferred to E5, CMake grant). |
| Environment | Windows 11 Pro 10.0.26200, MSVC 2022 (14.44) Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), Ninja. Detached worktree `.worktrees/review-stack-confirm-r3` at `b7ca003`. `scripts/build.ps1` changes directory to the worktree root, so the build directory is `.worktrees/review-stack-confirm-r3/build-review-stack-confirm-r3` (746/746 steps, exit 0, through `scripts/build-slot.ps1`). A second directory, `build-review-stack-confirm-r3-load`, was built only to load the machine during the stress runs. Targeted ctest ran through `scripts/gate-lock.ps1`. |
| ui-design-skills | Available at `~/.agents/skills/ui-design-skills/skills`. Read in full: `application-interface-design` and `responsive-layout`. I applied them to the tab strip (narrow layouts, "nothing essential clipped or unreachable") and to the chips (complete keyboard operation, no traps). Not read: `component-geometry`, `layout-spacing`, `typography-ui`, `motion-ui`, `design-review`. This round adds no new colour, type, radius or motion. |
| Evidence | Everything is in `docs/verification/STACK-CONFIRM-R3/`: targeted ctest log; stack_confirm_ui results file; review probes (`review-probes.diff.txt`, applied to the test file only while running, then reverted); probe results at `b7ca003` and with `ui/` from `509f6f9`; mutation script and results; stress-run summaries (250 runs); logs, thread states and the captured stack of the hung runs; the stack-dump tool source. |

## 1. Round 2 items

| Item | Status | Where / evidence |
| --- | --- | --- |
| R2-B1 (1) drop the one-photo stop | **fixed** | `ui/Main.qml:666-669`. Test `slideshowPassesThroughAOnePhotoFolder` |
| R2-B1 (2) "went nowhere" by path, not index | **fixed** | `ui/Main.qml:676-679`. Mutation M3b (index check restored) fails at `c-1.png`. M2 (check deleted) fails `input_ui` `slideshowAdvancesAndEndsCleanly` |
| R2-B1 (2), second sentence: a step of the slideshow's own that is still in flight is not a dead end | **open: R3-B1** | `ui/Main.qml:665`, `:678`. Probe P1 |
| R2-B1 (3) failure bound independent of folder size under "continue" | **fixed, not pinned** (R3-N3) | `ui/Main.qml:669`. Mutation M1 passes every test |
| R2-B1 (4) a(2)/b(1)/c(2) test with fail-before | **done** | the handoff's run with `ui/` at `509f6f9` stops at `b-1.png`. M3b fails here too |
| R2-N1 skip delay pinned | **fixed** | `slideshowPassesABadPhotoPromptlyAtASlowPace`. Mutation M7 fails (`5050 ms would have been sufficient`) |
| R2-N2 closing an inactive tab | **fixed** | `ui/Main.qml:684-690`. Mutation M4 fails `slideshowSurvivesClosingAnotherTab`. `slideshowEndsOnTabSwitchAndFolderOpen` still passes |
| R2-N3 clipped tab sliver | **fixed at 560 and 1180 px**, with two side effects (R3-N1, R3-N2) | `ui/Main.qml:1887`, `:1897-1911`. Mutation M5 fails at `width 1180, active tab 2: tabClose-0 is clipped at the left edge (x -18..14)`. The implementer's captures `tabs-r2-*.png` start on a clean tab boundary |
| R2-N4 no cue for other tabs at 560 px | **recorded** -> E5 (`lead_decisions_r2`) | |
| R2-N5 arrow keys between chips | **done** | `ui/PhotoGrid.qml:331`, `:345`, `:351-355`. Mutation M6 fails `typeChipsMoveWithTheArrowKeys`. Escape still returns to the grid without leaving grid mode, and the viewed photo does not change |

## 2. The hang in `slideshowKeepsGoingAcrossAFolderBoundary`

**Conclusion: it is a product hang.** The GUI thread waits forever on a named system semaphore. The same code runs in the real app on every photo step, and when the window closes. The test harness is not the cause, and `settleSlideshow` cannot prevent it, because the hang happens in the test body before `settleSlideshow` runs.

### Runs

The test function ran directly (`stack_confirm_ui_tests <function> -o <file>,txt`) with the ctest environment (`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`) and `QTEST_FUNCTION_TIMEOUT=90000`. Any run longer than 25-30 s had its threads sampled twice, 5 s apart. Script: `hangloop.ps1.txt`. Summary of every run: `hang-runs-summary.txt`.

| Batch | Binary / function | Load | Runs | Hangs |
| --- | --- | --- | --- | --- |
| idle-tip | `b7ca003` test as shipped (with `settleSlideshow`) | idle | 20 | 1 (run 9) |
| idle-nosettle | review probe P0: the same test without `settleSlideshow` | idle | 10 | 0 |
| load-tip | as shipped | full build of a second directory running in a build slot | 30 | 0 |
| load-nosettle | P0 | same load | 10 | 0 |
| load2-tip | as shipped | same load | 80 | 0 |
| idle2-tip | as shipped | idle | 100 | 1 (run 1) |
| **Total** | | | **250** | **2** |

Neither the loaded runs nor the runs without `settleSlideshow` hung more often. Both hangs used the shipped test with `settleSlideshow`.

### What the hung process was doing

- Both hangs are the same: `QFATAL ... Test function timed out` at 90 s, exit `0xC0000409`. The log stops after two `No QRhi found` warnings, where a passing run prints four, so the test was mid-slideshow and had not reached teardown (`hang-log-*.txt`).
- Process CPU time stayed exactly the same between the two samples (1546.875 ms at 30 s and at 35 s). Every thread was in `Wait` (`hang-threads-*.txt`). This is a blocked wait, not a loop.
- Stack of the GUI thread in the second hang. It was captured with a small dbghelp `StackWalk64` tool built for this review (`stackdump.cpp.txt`); the full dump is `hang-stacks-idle2-001.txt`:

  ```
  ntdll!NtWaitForSingleObject
  KERNELBASE!WaitForSingleObjectEx
  Qt6Core!...                         (QSystemSemaphore acquire)
  Qt6Core!QSharedMemory::lock
  Qt6Core!QSharedMemory::detach
  Qt6Core!QSharedMemory::~QSharedMemory
  stack_confirm_ui_tests_tip!<viewer controller code, no PDB>   x3
  Qt6Core!QObject::qt_static_metacall
  Qt6Core!QProcess::readyReadStandardOutput
  ...
  Qt6Core!QWindowsPipeReader::consumePendingAndEmit
  ...
  Qt6Core!QTest::qWait
  ```

### Mechanism (from the stack plus reading the code)

- On Windows, `QSharedMemory` guards `create`, `attach` and `detach` with a named `QSystemSemaphore`, a Windows semaphore with count 1. A semaphore has no owner, so if a process dies while it holds the count, the count is never given back.
- The decoder worker (`src/worker/main.cpp:196`, `:243`) attaches to the transfer buffer and detaches when it exits. Each time, it holds the semaphore briefly.
- The viewer kills a worker and then, synchronously and on the GUI thread, destroys its `QSharedMemory`:
  - `cancelLookahead()`: `retireIoProcess(...)` calls `process->kill()` (`src/app/io_job_lease.hpp:46`), then `prefetchTransfer.reset()` (`src/app/viewer_controller.cpp:659-663`).
  - `cancel()` does the same for the foreground decoder (`:676-684`).
  - The prefetch handler calls `cancelLookahead()` straight after writing `"ok\n"` (`:755-756`). Its worker then detaches and exits, so the kill can land while that worker holds the semaphore inside `detach()` or `attach()`.
  - Every `navigate()` also calls `cancelLookahead()` (`:1209`). A slideshow at a 1 s pace hits this window often.
- If the kill lands inside that window, the GUI thread's `~QSharedMemory` -> `detach()` -> `lock()` waits forever. The app freezes with no recovery. `~Private()` calls `cancel()` (`:490`), so closing the window during a decode or prefetch can freeze the same way.

This code predates STACK-CONFIRM-FIX; the round 2 diff does not touch it. The slideshow added in this stack exposes it, so it is listed as R3-B2.

## 3. Findings

### Blocking

**R3-B1 (MEDIUM-HIGH, open part of R2-B1): the slideshow stops silently when its own step is still in flight at the next tick, then the step lands with the slideshow off.**

- Where: `ui/Main.qml:665` waits only for `viewer.loading`, the decode flag. `:678` then treats `viewer.filePath === slideshowStepFrom` as a dead end. While a navigation job is running (`navigationBusy`, `viewer_controller.cpp:1079`), `loading` is false and `filePath` has not changed. `open()` sets `loading` only once the job has a destination (`:1125`, `:1692`). So the next tick reads an in-flight step as "went nowhere".
- Required by round 2: R2-B1 fix item 2 says "The check must not mistake a step of the slideshow's own that is still waiting for the next folder's listing for a dead end: under 'continue', next() can return before filePath changes." The handoff does not mention this clause.
- Evidence (review probe P1, `probeSlideshowAcrossASlowFolderStep`): folders `a` (2 photos), 300 empty sibling folders, then `b` (2 photos). Boundary "continue", pace **5 s (the default)**. Each step re-lists the folder, and the walk across empty folders re-lists the root, so the a-2 -> b-1 step takes about 5 s (`p1-tip-pace5-results.txt`):

  ```
  manual a-2 -> b-1 took 4953 ms (gap 300 empty folders)
  t= 0      a-1.png on=1
  t= 5026   a-2.png on=1 loading=1
  t= 15012  a-2.png on=0 loading=0 ready=1     <- stopped, no notice
  after stop + 8 s: b-1.png notice:            <- its own step then landed, slideshow off
  ```

  With 500 empty folders and a 1 s pace, it stops at `a-2.png` at t=3013 ms (`probes-tip-results.txt`). Slow listings come from many folders, large folders (each `step()` re-lists the whole folder, `navigation.cpp:406`) or network shares. In any of these cases the slideshow ends silently at a random photo, and the photo then changes on its own.
- The same result with `ui/` from `509f6f9` (`probes-base509-results.txt`): this is not a regression. It was in the base, and the old index check had the same flaw.
- Fix direction: QML cannot see whether a navigation is in flight, because no `Q_PROPERTY` exposes it (`include/glancelot/viewer_controller.hpp`). A clean fix needs the controller to report that the step has settled, either a `navigating` property or a signal for "boundary reached" or "step settled" (the controller already knows: `:1128` sets the "Folder boundary reached" status). The slideshow would treat the step as a dead end only after that report. This changes a shared header (lead-owned) and the hot file `viewer_controller.{hpp,cpp}`, so the lead decides the grant, or records a carve-out with a named later packet. The test is probe P1 at a 1 s pace, with a smaller gap so it runs quickly.

**R3-B2 (HIGH, product hang, predates this packet): the GUI thread can block forever in `QSharedMemory::~QSharedMemory` after a decoder or prefetch worker is killed.**

- Where: `src/app/viewer_controller.cpp:655-667` (`cancelLookahead`), `:668-685` (`cancel`), `:755-756`, `:1209`, `:490`; `src/app/io_job_lease.hpp:46`. See section 2 for evidence and mechanism.
- Effect: the whole window freezes permanently during ordinary photo stepping, most often during a slideshow, or when the window closes. It shows up 2 times in 250 runs of a 4 s test that makes about four steps.
- Fix direction (implementer's choice; it needs a grant for `viewer_controller.cpp` and possibly the worker):
  - Never run `QSharedMemory` detach or destruction on the GUI thread for a buffer whose worker was killed.
  - Or do not kill a worker that may be inside `attach` or `detach`: close its input, let it exit, and kill it only after a timeout.
  - Or replace the named-semaphore `QSharedMemory` transfer with a native mapping that needs no cross-process lock: `CreateFileMapping` with a handle the child inherits, or `memfd`/`shm_open` on Linux.
- Required test: a deterministic one. One option is a decoder test hook that holds the transfer semaphore and then is killed. The viewer must stay responsive, for example with a `QTRY` on an event-loop heartbeat. Show that the test fails on the current code.

### Non-blocking (fix with the above if convenient)

- **R3-N1 (LOW-MEDIUM, regression at the 540 px window minimum): the new `reveal()` loop can scroll the active tab completely out of view.** `ui/Main.qml:1904-1909` picks the first tab whose `x >= right - width`. When the active row (tab plus close button) is wider than the strip, that is the *next* tab, so the active tab disappears to the left. At 540 px (`ApplicationWindow.minimumWidth`, `ui/Main.qml:13`) the strip is 119 px wide and a row is 128 px (cap 96 + close 32). Probe P2, three long names, middle tab active:
  - `509f6f9`: `contentX=139 ... tab=-9..87` (87 of 96 px visible, close button visible)
  - `b7ca003`: `contentX=260 ... tab=-130..-34 close=..-2` (the strip shows tab 2 instead)

  Already in the base at 540 px: no placement fits the row, because the cap's floor `3 * controlHeight` plus the close button is wider than the strip, and the test checks only 560 and 1180. Fix: never choose a boundary to the right of `item.x`, and either let the cap shrink so that tab plus close fits the strip, or align to the row's left edge when it cannot fit. Add 540 to the widths in `longTabNamesKeepTheActiveTabInView`.
- **R3-N2 (LOW): the wheel handler ignores the new `rightMargin`.** `ui/Main.qml:1919` clamps to `contentWidth - width`. After `reveal()` scrolls into the margin, one wheel notch *toward the end* moves the strip backwards: probe P2 shows `wheel toward the end: contentX 226 -> 210`. That brings back a 16 px sliver of the previous close button at the left edge. Clamp to `contentWidth + rightMargin - width`, as `reveal()` does. Dragging can also show the empty margin, up to `tabCap + controlHeight` px. That is acceptable, but the wheel and drag clamps should agree.
- **R3-N3 (LOW, test gap): R2-B1 part 3 is not pinned.** Mutation M1 puts back `Math.min(slideshowFailureLimit, viewer.fileCount)` in every mode, and every slideshow test still passes. A one-photo folder with an undecodable photo under "continue", between good folders, must not stop the slideshow with "the last 1 photos could not be opened".
- **R3-N4 (info, found by reading the code, not run; the logic dates from round 1): `slideshowStepFrom` stays set until a step lands.** At a "stop" boundary that lasts a whole interval. During that time a manual folder change, or closing the active tab, is taken for the slideshow's own step (`ui/Main.qml:691-693`), and the slideshow carries on in the new place. When R3-B1 adds a "step settled" report, clear `slideshowStepFrom` on it too.
- **R3-N5 (info): the chips' Left and Right do not swap for right-to-left layouts** (`ui/PhotoGrid.qml:354-355`). The app ships only `en` and `en_XA` today.

### Checked without findings

- **Tab-count rule (R2-N2):** a lower tab count with a changed active index is taken as renumbering. Closing the active tab still ends the slideshow, through the folder change (or, if the remaining tab shows the same folder, it carries on there, which is acceptable). Opening a tab raises the count, so a switch still stops it (`slideshowEndsOnTabSwitchAndFolderOpen` passes). `slideshowTabs` is refreshed on every observation, so later closes compare against the current count.
- **Loop bound:** `max(2, min(5, fileCount))` only under "loop". By reading the code, a 3-photo loop with every photo bad stops after one pass. An 8-photo folder stops after five failures (`slideshowStopsAfterAnUnplayableRun`, which passes). Under "continue" and "stop" the bound is a flat 5. The notice always says "the last N photos", with N at least 2.
- **True dead ends:** under "stop", the tick after a step that went nowhere stops the slideshow (`input_ui` `slideshowAdvancesAndEndsCleanly`, pinned by M2). Under "continue", in probe P1 (when the step is fast enough) and in `slideshowPassesThroughAOnePhotoFolder`, it runs to the last photo of the last folder and stops there.
- **Chips (R2-N5):** Left and Right are claimed in `ShortcutOverride`, so the window's photo-step shortcuts do not fire while a chip has focus. Focus moves with `Qt.TabFocusReason`, so `visualFocus` shows the ring. At the ends the key is accepted and does nothing, with no wrap and no trap. The accessible description names the new keys, and `en_XA` carries the new string. No other catalog has the old string. `translation_coverage_ui` passes.
- **Tab boundary at 560 and 1180 px:** probe P2 at 560, 700 and 1180 px shows no left-edge sliver for any active tab, and the active tab and its close button are fully in view. The partial tab at the *right* edge is a useful sign that more tabs follow.
- **Grant check:** `git diff --name-only 509f6f9 b7ca003` lists only `tests/stack_confirm_ui_tests.cpp`, `translations/glancelot_en_XA.ts`, `ui/Main.qml`, `ui/PhotoGrid.qml`, the handoff and evidence. `ui/Main.qml` is a hot file, and the changes stay inside the slideshow block and the tab strip, as in round 2.

## 4. Fail-before spot checks

Each product hunk was reverted locally and then restored with `git checkout HEAD -- ui` (`mutations-results.txt`; `git status --short` afterwards showed only the evidence folder):

| Mutation | Test | Result |
| --- | --- | --- |
| M1 failure bound back to `min(5, fileCount)` in every mode | all six slideshow tests | **PASS: not pinned (R3-N3)** |
| M2 the path-based "went nowhere" stop deleted | `input_ui` `slideshowAdvancesAndEndsCleanly` | FAIL, `'!flag(h, "slideshowOn")' returned FALSE` |
| M3b the index-based check of the base put back | `slideshowPassesThroughAOnePhotoFolder` | FAIL, `Actual "c-1.png"`, expected `c-2.png` |
| M4 tab-count rule reverted | `slideshowSurvivesClosingAnotherTab` | FAIL, `closing an inactive tab ended the slideshow` |
| M5 `rightMargin` removed | `longTabNamesKeepTheActiveTabInView` | FAIL, `width 1180, active tab 2: tabClose-0 is clipped at the left edge (x -18..14)` |
| M6 chip `Keys.onRightPressed` removed | `typeChipsMoveWithTheArrowKeys` | FAIL, `'chip("image")->hasActiveFocus()' returned FALSE` |
| M7 skip timer start removed | `slideshowPassesABadPhotoPromptlyAtASlowPace` | FAIL, `5050 ms would have been sufficient` |

(A first M3 attempt applied the index check only while a step was pending, which is not what the base did, and it passed. M3b uses the check exactly as the base had it.)

## 5. Test output

Targeted suites, Windows, offscreen (CMake test environment), under the gate lock, first run, no reruns:
`& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review-stack-confirm-r3 -Run { ctest --test-dir ...\build-review-stack-confirm-r3 --output-on-failure -R "^(stack_confirm_ui|input_ui|folder_grid|tabs_recovery|translation_coverage_ui|viewer_acceptance|command_routing)$" }`

```
1/7 Test  #7: viewer_acceptance ................   Passed   11.78 sec
2/7 Test #12: folder_grid ......................   Passed  229.30 sec
3/7 Test #13: input_ui .........................   Passed   84.50 sec
4/7 Test #44: tabs_recovery ....................   Passed   59.52 sec
5/7 Test #47: translation_coverage_ui ..........   Passed   24.57 sec
6/7 Test #49: command_routing ..................   Passed   10.06 sec
7/7 Test #52: stack_confirm_ui .................   Passed   60.75 sec

100% tests passed out of 7
Total Test time (real) = 480.87 sec
```

Review probes (temporary additions to `tests/stack_confirm_ui_tests.cpp`, rebuilt through `build-slot.ps1`, then reverted). At `b7ca003`:

```
FAIL!  probeSlideshowAcrossASlowFolderStep  (R3-B1: stopped at a-2.png; gap 500 at 1 s pace, and gap 300 at the default 5 s pace)
FAIL!  probeTabStripAtWindowMinimum         (R3-N1: 6 placements at 540 px leave the active tab out of view; 560/700/1180 OK)
PASS   probeGuardWithoutSettle              (20 runs, used for the hang counts)
```

With `ui/` from `509f6f9`, both probes fail the same way, except that at 540 px the middle tab is clipped by 9 px instead of being scrolled out of view (`probes-base509-results.txt`).

## 6. To approve

1. R3-B1: the slideshow must not treat an in-flight step of its own as a dead end. This needs a controller report of "step settled / boundary reached", and so a lead grant for `include/glancelot/viewer_controller.hpp` and `src/app/viewer_controller.{hpp,cpp}`. If the lead does not want that change in Phase 1, it must record a carve-out in the queue row with a named packet. Add probe P1 as a test.
2. R3-B2: remove the forever wait in `QSharedMemory` teardown after a worker is killed, with a deterministic test that fails first. It sits in the decoder and prefetch path of `viewer_controller.cpp`, not in this packet's files. The lead decides whether STACK-CONFIRM-FIX takes it (with a grant) or a separate packet does, but STACK-CONFIRM cannot be approved while a known permanent GUI freeze is reachable through its slideshow.
3. Recommended in the same change: R3-N1 (540 px), R3-N2 (wheel clamp), R3-N3 (pin the failure bound).

A fresh reviewer then re-reviews the slideshow block, the controller change and the decoder teardown. The chips and the tab-count rule need no further review.

## Unverified

- Docker/Linux: neither the new geometry assertions nor the shared-memory hang were run on Linux. On POSIX, `QSharedMemory` uses `QSystemSemaphore` too (a System V or POSIX semaphore, depending on the key type), so a similar hang is plausible but not shown.
- The first hang's stack was not captured (the thread-state sample shows the same blocked pattern). Only the second has a symbolized stack. The test executable has no PDB, so the three viewer frames are not named. From the call path (inside a `readyReadStandardOutput` slot, destroying a `QSharedMemory`), they are `cancelLookahead()`/`cancel()` or the decoder output handler.
- Screen readers, physical displays and fractional DPI were not checked. The full suite was not rerun by this review; the handoff's full run on `607546c` was 52/52.
