# STACK-F015-RECOVERY review R2 (re-review of the "Review round 2 fixes")

**Verdict: CHANGES REQUIRED.** One blocking item, B2. The round-2 fix closes B1 exactly as the round-2 review reproduced it, and every path the brief listed holds. But the `openSerial` fence counts every `open()`, including the viewer's own reopen of the image it is already showing. After a folder-route switch to an unavailable folder, the window still shows the previous tab's image. If an external edit or deletion in that image's folder, or an F5, then reopens or clears that image, the target tab is settled on the previous tab's location. The destructor then marks that record `clean`. A scratch probe reproduces this deterministically on `9c8d952` in 4 of 4 rows. Two of those rows involve no user action at all. This is the same failure row as B1 (folder unavailable, the target's location lost permanently, wrong record marked clean), reached by a different trigger. It was already present in round 1, so it is not a regression of round 2, but it is still open.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review session. It did not implement this packet and did not do either earlier review. |
| Branch reviewed | `origin/claude/stack-f015-recovery` at `9c8d952` |
| Round-2 diff | `git diff 5396de7 9c8d952` (product: `src/app/viewer_controller.cpp`; tests: `tests/tabs_recovery_tests.cpp`, `tests/window_slot_tests.cpp`) |
| Whole packet | `git diff b610348 9c8d952 -- . ':!docs'`. This is `b13f6c1..9c8d952` without the F040 merge `5396de7`; F040's tip is `b610348`. |
| Prior review | `origin/claude/review-stack-f015` `6d882b2`, `STACK-F015-RECOVERY-REVIEW.md` (B1, N1, N3) |
| Contract | `LEAD-WRAPUP-2026-10-22.md` (F015 bullet), plus the lead decisions restated in this review's brief |
| Date and environment | 2026-10-23, Windows 11, MSVC 2022, Qt 6.8.3 (`.tools`), Ninja Release, offscreen |

## Blocking

### B2 (High, same failure row as B1): a reopen of the image the window already shows settles a failed folder-route switch

**Where.** `src/app/viewer_controller.cpp`:

- `notePosition()`, line 448: `if(tabSettling&&openSerial==settleSerial)return;`
- `open()`, line 1617: `++d->openSerial` on every call.
- The directory-visit branch at line 1051, which clears `tabSettling` unconditionally.

**Mechanism.**

1. `activateTab()` or `closeTab()` of the active tab targets a tab whose image is gone. It calls `navigateDirectory(target.folder)`, and that folder is unavailable (a removed folder, or an offline network share or removable drive).
2. The navigation fails. `path` stays the previous tab's image, the canvas keeps showing it, and `observe()` keeps watching the previous tab's folder.
3. Settling never ends, and the round-2 fence then only asks whether `open()` ran again since the switch.
4. Several non-tab sites call `open()` on that same stale `path`, which advances `openSerial`:
   - `reconcileObservation()`: line 866 (forced refresh, revision change or file event), line 853 (renamed image), line 861 (case-only rename) and line 820 (renamed directory).
   - `reconcileRenamedPath()`, line 2086.
5. The resulting decode passes the fence, and `notePosition` writes the previous tab's location into the target's slot.
6. An external deletion of that image goes through `reconcileRemovedPaths()` instead. Its directory visit lands in the line-1051 branch, which settles on the previous tab's folder with an empty image. The fence does not cover that branch at all.

So the answer to the brief's question is: wraparound cannot fool the fence, but **a reopen of the same image can**.

**Reproduction.** A scratch test, not committed. It was added to `tests/tabs_recovery_tests.cpp` in this review's worktree with the controller unchanged at `9c8d952`. Only `tabs_recovery_tests` was rebuilt, and the test was run under `gate-lock.ps1 -Branch review/f015-r2`. Abridged:

```cpp
QVERIFY(openThreeTabs(config.path(), viewer, media, 2));        // alpha, beta, gamma; active gamma; saved
QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
if (stale) { viewer.open(files[0]); QVERIFY(viewer.loading()); } // row 4 only: B1's in-flight decode as well
QVERIFY(viewer.activateTab(1));                                  // folder route; beta's folder is gone
QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
// precondition, asserted: live tabs and the stored record both hold beta for tab 1 ("Cannot open that folder")
// then one of:  QImage(...).save(files[2])  |  QFile::remove(files[2])  |  viewer.trigger("refresh")
QTest::qWait(4000); QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
// destroy; expected stored tabs [alpha, beta, gamma] (row 4: [alpha, beta, alpha]) active 1, record clean
```

The output below is abridged, with the temporary directory written as `<tmp>`. Each row also logged that a reopen really happened: `loading` was seen and `imageChanged` fired. Each row's precondition held: the stored record held beta for tab 1 before the trigger.

```
[B-probes-tip] exit 4
QINFO  : probeReopenAfterFailedFolderSwitch(external edit) probe "edit" before: live and stored hold beta for tab 1; status "Cannot open that folder. Check the path and access permissions."
QINFO  : ... "edit" after: reopened(loading seen) true imageChanged 1 showing "<tmp>/gamma/c.png"
QINFO  : ... "edit" live: "live tab 1 holds the wrong location: live tabs [<tmp>/alpha/a.png, <tmp>/gamma/c.png, <tmp>/gamma/c.png] active 1"
QINFO  : ... "edit" record state "clean"
FAIL!  : probeReopenAfterFailedFolderSwitch(F5 refresh)            wrong position: stored tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 1, position gamma/c.png   (record "clean")
FAIL!  : probeReopenAfterFailedFolderSwitch(external edit)         wrong position: stored tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 1, position gamma/c.png   (record "clean")
FAIL!  : probeReopenAfterFailedFolderSwitch(external delete)       wrong position: stored tabs [alpha/a.png, "" in <tmp>\gamma, gamma/c.png] active 1, position "" in <tmp>\gamma   (record "clean"; status "No photos are left in this folder.")
FAIL!  : probeReopenAfterFailedFolderSwitch(F5 after stale decode) wrong position: stored tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1, position alpha/a.png   (record "clean")
Totals: 2 passed, 4 failed, 0 skipped, 0 blacklisted, 24469ms
```

**Why this blocks.**

- The "external edit" and "external delete" rows need no action in the window. Another program writing or removing a file in the folder the window happens to show is enough. A tethered capture folder or a sync client would do it.
- F5 is arguably user intent. It is also the natural "retry" after "Cannot open that folder", and it silently discards the tab the user was trying to reach.
- In every row, beta's location is lost in memory and on disk, and the destructor marks the record `clean`. That is the outcome B1 was raised for.

**Suggested fix.** This is the implementer's choice, inside the tab regions plus the declared one-token sites.

- **Option 1.** Make a settle depend on what was opened, not only on how many opens happened. For example, record the pre-switch `path` in `settleOn()` and do not settle while the decoded file is still that path. Also let the line-1051 branch settle only for the switch's own navigation, for example by recording the `navigationGeneration` that the folder route submitted.
- **Option 2.** Do not count reconcile-driven reopens (`reconcileObservation`, `reconcileRenamedPath`, `reconcileRemovedPaths`) as settle triggers. Bump `openSerial` only for tab, user and navigation opens.
- **Option 3.** When the folder route fails during a switch, clear the stale image so nothing of the previous tab remains to be reopened. This changes what the user sees, so it needs a lead decision.

Whichever option is chosen, add this probe's rows as tests: F5, external edit, external delete, and stale decode plus F5. A renamed-image row would be a useful addition. The rows must fail on `9c8d952`.

## Verified

### B1 is closed for every path in the brief

Each path below was traced in the code at `9c8d952`. Paths marked "test" are also covered by a committed test that passes on the tip and fails without the fix.

| Path | Result |
| --- | --- |
| Switch, folder route, stale decode in flight | The decode lands with `openSerial==settleSerial` and is fenced. `lastDecodedPath` updates, the position does not. Test: `staleDecodeCannotSettleADirectorySwitch(switch)`. |
| Close the active tab, same case | Same fence. Test: `staleDecodeCannotSettleADirectorySwitch(close the active tab)`. |
| Close an inactive tab mid-switch | There is no `settleOn`, so `settleSerial` is kept and the fence still applies. Test: `secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(close another tab)`. |
| Second action before the target decodes (`newTab`, `activateTab`, `closeTab`) | `activeLocation()` returns `pendingFolder`/`pendingImage`. A second `activateTab` re-records `settleSerial`. On the image route, the first target's decode is cancelled by the second `open()`. On the folder route it is fenced, because it was issued before the second switch. Test: all four rows. |
| Image route | `open(target)` advances `openSerial` and cancels the old decode (`++generation; cancel()`). Only the target's decode can settle it. |
| Late fs-watch baseline (line 375) during a folder route | It runs `++generation` and re-decodes the previous `path` without calling `open()`, so it is fenced. This is the right reason to prefer a separate counter over `generation`. |
| Target decode fails | `fail()` touches neither the pending location nor the settle state. Test: `switchingToAnUndecodableTabKeepsEveryTabsLocation` (round 1). |
| Folder unavailable, nothing else happens | The location is kept and the record is marked clean correctly (test). **Exception: B2.** |
| Folder lists images (transient form) | The navigation result calls `open(first)`, which advances the serial, and that decode settles. A destructor that runs before then saves the stored target. Reasoned only, as the handoff says. |
| Folder has no images | The line-1051 branch bumps `generation` and cancels, then settles on the visited directory. This is correct for the switch's own visit. **B2:** it also accepts a reconcile's visit. |
| Claim retry (`app/main.cpp:211`) mid-switch | `setWindowClaim` now calls `replayPosition()`. While settling, this only marks the position dirty and keeps `pendingFolder`/`pendingImage`, which is the stored target. Test: `claimRetryDuringATabSwitchKeepsEveryTabsLocation`. |

**Can `openSerial` be fooled?**

- **Wraparound:** no. It is a `quint64` that goes up once per `open()`; 2^64 opens is unreachable. The fence is an equality test against the count at the switch, so any later open passes it and no earlier result does.
- **Reopen of the target's own image:** harmless. The target's image is the correct place to settle.
- **Reopen of the previous tab's image after a failed folder route:** this fools the fence. That is B2.

### The edits outside the tab regions change nothing else

- **`++openSerial` in `open()`.** `openSerial` is read only in `settleOn()` (line 197) and `notePosition()` (line 448), as `grep openSerial src/` shows. `notePosition`'s fence is guarded by `tabSettling`, so outside a settle `notePosition` behaves exactly as before.
  - Presentation, reference and monitor output take `generation` through `displayManager.updateImage(path, image, generation)`. None of them sees `openSerial`.
  - No other file calls `ViewerController::open`. The QML callers (Filmstrip, PhotoGrid, the drop handler, the open dialog and the `previewDebounce` cursor preview at `Main.qml:146`) are all user-driven.
  - Main changed nothing in the tab regions or `open()` since the merge-base `afeac1a`: its three controller commits are EN-05 and EN-06.
- **`setWindowSlot()`, both sites.** With `tabSettling` false, `replayPosition()` is the old `if(!lastDecodedPath.isEmpty()) notePosition(lastDecodedPath)`, so nothing changes.
- **`setWindowClaim()`.** With `tabSettling` false, the condition reduces to the old `!lastDecodedPath.isEmpty()`, and the folder branch is unchanged.
  - With `tabSettling` true, it marks the restore or switch target dirty instead of replaying the last decoded image.
  - A single-location window can be settling after `restoreWindow()`'s `settleOn`. In that case it writes the restore target, which is the stored position anyway.
  - `app/main.cpp` never reaches that state, because a busy first claim leaves slot 0 and no restore runs before the retry. This is harmless (see Non-blocking).
- **The no-claim writer** (lead decision accepted) now writes `folder`/`image`/`saved` only, and replacing the entry drops stale tabs. Test: `noClaimWriterNeverWritesTabs`.
- **`window_slots`'s `tabsKeepLocationsAndRestoreWithTheWindow` port** (lead decision accepted). Every assertion is unchanged; only the setup moved from `setWindowSlot(6)` to a claimed slot.

### Nothing marked clean with wrong content, apart from B2

- `writePosition(true)` accepts `checkpointInput()` and passes `allowClean = !positionDirty`. `checkpointInput` always writes `tabs[activeTabIndex] = {pendingFolder, pendingImage}`, so `validTabs` coherence holds by construction.
- The round-1 checkpoint-trigger table (`6d882b2`) still holds for round 2.
- The only way found to put wrong content in `pendingFolder`/`pendingImage` while the record is cleaned is B2.

### The whole packet, re-checked on F040's tip

`git diff b610348 9c8d952 -- . ':!docs'` changes nine files:

- the public header (unchanged from the approved round-1 diff: `TabLocation`, `MaxTabs`, and the appended defaulted `tabs`/`activeTab`)
- `recovery_checkpoint.cpp` (`validTabs`, `readTabs`, the atomic tab write in the same `transact`)
- `recovery_session.cpp` (`readOnlyPosition` reads tabs)
- the internal `recovery_tabs.hpp`
- the controller
- the two test files
- the provisional `CMakeLists.txt` include and `cmake/TabsRecoveryTests.cmake` (N2, still the lead's)

There are no other changes, and the round-1 findings on these files stand.

### The new tests fail on the unmodified controller

Each run below kept the tests at `9c8d952` (plus the probes) and changed only `src/app/viewer_controller.cpp`. Only `tabs_recovery_tests` was rebuilt, and everything ran in one `gate-lock` block.

**C: the round-2 controller reverted to `5396de7`** (`git checkout 5396de7 -- src/app/viewer_controller.cpp`): 5 failed out of 10.

```
[C-reverted] exit 5
FAIL!  : staleDecodeCannotSettleADirectorySwitch(switch) 'live.isEmpty()' ... live tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1
FAIL!  : staleDecodeCannotSettleADirectorySwitch(close the active tab) ... live tabs [alpha/a.png, alpha/a.png] active 1
FAIL!  : secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(stale decode, new tab) ... live tabs [alpha/a.png, beta/b.png, alpha/a.png, alpha/a.png] active 2
FAIL!  : claimRetryDuringATabSwitchKeepsEveryTabsLocation() 'differ.isEmpty()' ... stored tabs [gamma/c.png, beta/b.png, gamma/c.png] active 0, position gamma/c.png
FAIL!  : noClaimWriterNeverWritesTabs() ... {"activeTab":1,...,"tabs":[{...alpha...},{...beta...}]}
Totals: 5 passed, 5 failed, 0 skipped, 0 blacklisted, 7948ms
```

This matches the handoff's fail-before result exactly. The three non-stale second-action rows pass here by design, and the handoff's M3 shows they can fail.

**D: the tip controller with only the `notePosition` fence line removed:** 3 failed out of 9.

```
[D-nofence] exit 3
FAIL!  : staleDecodeCannotSettleADirectorySwitch(switch)   ... live tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1
FAIL!  : staleDecodeCannotSettleADirectorySwitch(close the active tab) ... live tabs [alpha/a.png, alpha/a.png] active 1
FAIL!  : secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(stale decode, new tab) ... live tabs [alpha/a.png, beta/b.png, alpha/a.png, alpha/a.png] active 2
Totals: 6 passed, 3 failed, 0 skipped, 0 blacklisted, 7430ms
```

In D, `claimRetry...` passes, so the replay guard works independently of the fence; this matches the handoff's M4.

**E: restored to the pristine tip and rebuilt:** `Totals: 10 passed, 0 failed`. The worktree was clean afterwards (`git status --short` empty at `9c8d952`).

## Windows test run (pristine `9c8d952`)

**Build.** `build-slot.ps1 -Run { … scripts\build.ps1 -BuildDir build-review-f015-r2 }`: 589/589 targets, exit 0.

**Targeted ctest.** Run inside `gate-lock.ps1 -Branch review/f015-r2`, with PATH prefixed by `.tools\python\Scripts` and the Qt `bin`:

```
1/9 Test  #7: viewer_acceptance ................   Passed    9.65 sec
2/9 Test  #9: folder_tree ......................   Passed    9.84 sec
3/9 Test #11: window_slots .....................   Passed   33.28 sec
4/9 Test #38: recovery .........................   Passed   15.82 sec
5/9 Test #39: recovery_startup .................   Passed    1.07 sec
6/9 Test #40: recovery_controller ..............   Passed   17.56 sec
7/9 Test #41: recovery_process .................   Passed   44.15 sec
8/9 Test #42: tabs_recovery ....................   Passed   32.11 sec
9/9 Test #45: reference_pin ....................   Passed   21.29 sec
100% tests passed out of 9
Total Test time (real) = 185.37 sec
tabs_recovery: Totals: 59 passed, 0 failed, 0 skipped, 0 blacklisted, 31949ms
reference_pin: Totals: 10 passed, 0 failed, 2 skipped (nativeTextureReleasedOnClose/OnShed: need a native RHI backend; the software backend has none)
```

## Integration notes for the lead

**Conflict.** `git merge-tree --write-tree --name-only origin/main origin/claude/stack-f015-recovery` (main `fc1660e`) reports one conflict, in `CMakeLists.txt`. Main's EN-06 block (`if(BUILD_TESTING) include(cmake/CommandRoutingTests.cmake) endif()`) and F040's `include(cmake/ReferencePinTests.cmake)` sit at the same spot. Resolve it by keeping both. `src/app/viewer_controller.cpp` and `ui/Main.qml` auto-merge.

**Adaptation required (EN-05).**

- Main's `9c7b203` moved the decode cache into `src/app/decode_cache.cpp` and introduced `GLANCELOT_VIEWER_SOURCES`.
- Both provisional targets list `viewer_controller.cpp` by hand, so they no longer link on the merged tree. On a scratch merge with only the conflict resolved, linking `tabs_recovery_tests` fails:

  ```
  viewer_controller.cpp.obj : error LNK2019: unresolved external symbol "public: class QImage __cdecl glancelot::detail::DecodeCache::reuse(...)"
  viewer_controller.cpp.obj : error LNK2019: unresolved external symbol "public: bool __cdecl glancelot::detail::DecodeCache::insert(...)"
  tabs_recovery_tests.exe : fatal error LNK1120: 2 unresolved externals
  ```

- The fix:
  - In `cmake/TabsRecoveryTests.cmake`, replace the `viewer_controller` line with `${GLANCELOT_VIEWER_SOURCES}`.
  - In `cmake/ReferencePinTests.cmake`, replace the `viewer_controller` and `settings_file` lines with the same variable.
  - CMake de-duplicates `settings_file.cpp` against `glancelot_recovery_provider_sources`, as in `recovery_controller_tests`.
- With that change, both targets build on the scratch merge: 79/79, exit 0.

**Labels (EN-03).**

- Neither provisional file calls `glancelot_test_labels`, so both tests would be computed as `fast`.
- Both start `glancelot-decoder` through `ViewerController::open`, so add:
  - `glancelot_test_labels(tabs_recovery LABELS process)`
  - `glancelot_test_labels(reference_pin LABELS process)`
- Neither loads `ui/Main.qml`, and neither asserts a wall-clock budget. `reference_pin`'s `BudgetScope` is the display manager's image-memory budget, not a timing budget. `monitor_output`, which has the same shape, is unlabelled on main.
- On the scratch merge, `ctest -N -L process -R '^(tabs_recovery|reference_pin)$'` lists both.

**Scratch merge.** The scratch merge is local only and was not pushed. It is `c481611` in `.worktrees/review-f015-r2-merge`: `origin/main` plus `9c8d952`, with the conflict resolved and the two cmake adaptations applied. The tests were not run there.

**Ownership.** N2 is unchanged: both cmake files and the root include are lead-owned.

## Non-blocking

- **N4 (Low): a stale image under the target tab.** After a failed folder route, the canvas and the window title keep showing the previous tab's image while the target tab is active. Data is safe apart from B2, but the user sees one tab's image under another tab's label. Option 3 under B2 would remove this as well.
- **N5 (Info): the restore-settle replay.** `replayPosition()` while a single-location window is settling after `restoreWindow()` marks the slot dirty with the restore target, where the old code replayed nothing. The content is the stored position, and `app/main.cpp` cannot reach this state (see above). No action needed.
- **N6 (Info): the transient B1 form is still reasoned only.** This is the case where the folder lists images and the destructor runs before the navigation result. I agree with the handoff's reasoning; it was not tested here either.

## Unverified

- Physical, multi-window and GPU paths were not exercised. `reference_pin` skips two native-RHI cases offscreen.
- The full offscreen suite was not run. Only the targeted set required by the brief was run, and the authoritative gate decides on the integrated commit.
- No tests were run on the scratch merge, only the build of the two targets.
- B2 is shown on `9c8d952`. That it is also present on round 1 (`8afe4cd`) is from code reading: round 1's `notePosition` cleared the settle on every decode, and the line-1051 branch is round-1 code. It was not run there.
