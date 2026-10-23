# STACK-F015-RECOVERY review (round 2)

**Verdict: CHANGES REQUIRED.** One blocking item: the round-1 High is closed for the paths it named, but a narrower form of the same defect remains. When a tab switch falls back to `navigateDirectory`, a decode of the previous tab's image that is still running is not cancelled. It lands, settles the switch on the wrong file and writes that tab's location into the target tab's slot. The destructor then marks that record clean. A deterministic scratch test reproduces it 3 of 3 runs on the reviewed code. Everything else in round 1 is correct and verified, including the header diff the lead approved.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review session. It did not implement this packet and did not do the stage-1 review. |
| Branch reviewed | `origin/claude/stack-f015-recovery` at `8afe4cd` (round-1 fix `0111e15`) |
| Review diff | `git diff b13f6c1 8afe4cd` (F015's own changes on F040 implementation commit `b13f6c1`) |
| Scratch merge tested | `review/f015-merge` = `8afe4cd` + `origin/claude/stack-f040-pinned` at `b610348`, merge commit `e0adf16`. Local only, not pushed. |
| Contract | `LEAD-WRAPUP-2026-10-22.md` (F015 bullet), `LEAD-TAKEOVER-2026-10-22.md` section 4 |
| Date | 2026-10-23, Windows 11, MSVC 2022, Qt 6.8.3 (`.tools`), Ninja Release, offscreen |

## Stage-1 review (round 1) summary

The first review was not committed as a document. Its summary is included here so the record is complete.

- **HIGH.** `activateTab` set the active index before `pendingFolder`/`pendingImage` settled. A checkpoint taken mid-switch or mid-close therefore wrote the old tab's location into the target slot, and the destructor could mark that wrong record clean.
- **LOW, public `readTabs`.** `readTabs` was public, which goes beyond the contract. Lead decision: move it to an internal header.
- **LOW, legacy restore.** The legacy no-claim restore path lacked validation. Lead decision: keep the path and validate it like the claimed path.
- **LOW, tests.** Four tests could not fail.
- **LOW, literal.** The code used the literal `8` instead of `MaxTabs`.
- **Header.** The lead approved the header additions `TabLocation` and `MaxTabs`, and the appended `tabs`/`activeTab` fields.

## Blocking

### B1 (High, residual of round-1 High): a stale decode from the previous tab settles a directory-route switch

**Where:** `src/app/viewer_controller.cpp`.

- `activateTab()` (about line 1496) and `closeTab()` of the active tab (about line 1514) fall back to `navigateDirectory(target.folder)`. The fallback runs when the target's image is empty or no longer exists.
- `navigateDirectory()` (line 1724) does not bump `generation` and does not cancel the running decode. It also leaves `path` unchanged.

**What goes wrong:**

1. A decode for the previous tab's file is in flight at the switch. Example: the user opened a file and pressed Ctrl+Tab before it decoded.
2. That decode still matches `generation` and completes normally.
3. The decode reaches `notePosition(path)` (line 990, or 941 on a cache hit). `notePosition` clears `tabSettling` and sets `pendingFolder`/`pendingImage` to the previous tab's file while `activeTabIndex` is already the target.
4. From then on, `checkpointInput()` writes the previous tab's location as `tabs[target]` and as the slot position.

When does it get corrected?

| Target folder | Effect |
| --- | --- |
| Lists with images | The navigation result opens a file and settles correctly. There is a transient wrong window. |
| Has no images | The folder branch at line 1040 corrects it. There is a transient wrong window. |
| Is unavailable | Nothing corrects it. The target's stored location is lost permanently, and the destructor's `writePosition(true)` → `close(1000, true)` marks the wrong record `clean`. |

This contradicts the handoff's statement that settling holds "if the target fails to decode or its folder is unavailable".

**Reproduction.** Scratch test, not committed. It was added to `tests/tabs_recovery_tests.cpp` in a scratch copy of `e0adf16`, with the source unchanged:

```cpp
QVERIFY(openThreeTabs(config.path(), viewer, media, 2));      // alpha, beta, gamma; active gamma; saved
QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
viewer.open(files[0]);            // tab 2 moves to alpha; its decode is in flight
QVERIFY(viewer.loading());
QVERIFY(viewer.activateTab(1));   // beta's image is gone: navigateDirectory, no generation bump
QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(), 10000);
// destroy; expected stored tabs [alpha, beta, alpha], active 1, position beta
```

```
FAIL!  : TabsRecoveryTests::probeInFlightDecodeLandsAfterDirectorySwitch() 'differ.isEmpty()' returned FALSE.
         (wrong position: stored tabs [alpha/a.png in alpha, alpha/a.png in alpha, alpha/a.png in alpha] active 1,
          position alpha/a.png in alpha)
Totals: 2 passed, 1 failed ... (run 1)   Totals: 2 passed, 1 failed ... (runs 2 and 3)
QINFO  : ... probe a record state "clean"
```

The `clean` line comes from the mutation runs below, where `activateTab` was unmodified. The live tab strip shows the same thing: all three tabs report `alpha`. Beta's location is gone in memory as well as on disk.

**Control.** The same switch with no decode in flight passes. That test removes beta, settles on gamma, runs `activateTab(1)` and waits for "Cannot open that folder". `navigateDirectory`'s failure path calls `scan()`, which is listing only and never reaches `notePosition`. The `settleOn` fix itself is therefore sound. The gap is the stale decode alone.

**Suggested fix.** This is the implementer's choice, inside the tab regions.

- **Option A.** Make a tab switch retire the previous tab's decode before the directory fallback, as `open()` does with `++generation; cancel()`.
- **Option B.** Let `notePosition` clear `tabSettling` only for results issued after `settleOn`, using a settle token or generation recorded in `settleOn`.

Either way, add this case as a test. The scratch test above fails today and should pass after the fix. Also cover the transient case, where the folder lists and the destructor runs before its navigation result.

## Non-blocking

- **N1 (Low): the handoff understates a production path for the claim replay.**
  - Under "Unverified / notes (round 1)", the handoff says no production path reaches `setWindowClaim`/`setWindowSlot` mid-switch.
  - `app/main.cpp:211` retries a busy startup claim every 4 s, up to 8 times. Each retry calls `setWindowClaim` and then `restoreWindow`, so tabs can already exist when it runs.
  - `setWindowClaim` replays `notePosition(lastDecodedPath)` (line 1230). A retry that fires mid-switch settles on the previous tab's last decoded image.
  - The trigger is narrow and the next settled navigation corrects it. Correct the note, or skip the replay while `tabSettling` (use `pendingFolder`/`pendingImage`), when B1 is fixed.
- **N2 (Info): lead-owned build files.** `CMakeLists.txt` (one `include`) and `cmake/TabsRecoveryTests.cmake` are lead-owned. They are provisional, as the handoff states, and the lead decides.
- **N3 (Info): the no-claim writer still writes tabs.** It does so through `updateSettings`. It now uses the same `checkpointInput()` snapshot, and the handoff asks the lead whether to retire it. It is not a second writer next to a session.

## Verified

### Round-1 items

- **Item 1, High fix.** `settleOn()` is called in `activateTab()`, in `closeTab()` of the active tab and in `restoreWindow()`. `activeLocation()` is used by `newTab`/`activateTab`/`closeTab`/`tabs()`. `checkpointInput()` always writes `tabs[activeTabIndex] = {pendingFolder, pendingImage}`, so `validTabs()` coherence holds by construction. Every checkpoint trigger is listed below.

  | Trigger | Result |
  | --- | --- |
  | Destructor (`writePosition(true)`) | Saves the target's stored location for the target index. `close(…, !positionDirty)` cleans only if that newest snapshot was accepted. |
  | Decode failure | `fail()` does not touch pending or settling, so the stored location is kept. The undecodable test covers this. |
  | `newTab()` mid-switch | Both copies take the stored target, and later settling applies to the new tab. Correct. |
  | Close an inactive tab mid-switch | No `settleOn`. Pending stays the target. If one tab remains, the single location is pending, which is the target. |
  | Close the active tab | `settleOn(neighbour)`. The closed tab is never saved. |
  | `retireRecovery()` | `writePosition()` then `close(0, !positionDirty)`, with the same snapshot. |
  | SAFE | `restoreWindow()` returns before any tab or settle change. The stored set is left byte-equal (test). |
  | Reset | The file is set aside with its tabs, and the new claim restores nothing (test waits for the dirty marker). |

  The exceptions are B1 and N1.
- **Atomicity.** `checkpoint()` writes `tabs`/`activeTab` inside the same `settings_detail::transact` as `folder`/`image`/`saved` and the record's `checkpointRevision`/`sequence`. Claim and sequence checks are unchanged. `tabsAndPositionLandInOneTransaction` shows one replace with the before-state entirely old and the after-state entirely new, and a `BeforeReplace` failure leaves the bytes unchanged.
- **No clean before the newest snapshot.** `Session::accept` keeps only the newest immutable `CheckpointInput`, which carries the tabs. The destructor and retire paths accept before `close`, and pass `allowClean = !positionDirty`. `failedTabCheckpointNeverCleansAndRetrySucceeds` and `busyWriterIsRetriedAndLatestPendingTabsWin` cover this. The busy test asserts that sequences 1 and 2 never reached disk.
- **Item 2.** `readTabs` is in the internal `src/app/recovery_tabs.hpp`.
- **Item 3.** The legacy no-claim restore calls the same `recovery::readTabs`. `legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath` runs 14 rows.
- **Item 4.** The four strengthened tests now assert observable outcomes: unchanged bytes and `persistedSequence == 0`, stored-then-absent tabs after the begin, a single replace, and absent intermediate sequences.
- **Item 5.** `newTab()` uses `recovery::MaxTabs`.
- **`readTabs` validation.** It requires an array of 2..8 records, an integral in-range `activeTab`, string `folder`/`image` values that are absolute, contain no NUL and are at most 32768 characters long, and an active record equal to the position. Anything else gives one location and never fails the begin.

### Final public header diff (`git diff b13f6c1 8afe4cd -- include/glancelot/recovery_checkpoint.hpp`)

The diff matches the lead decision exactly:

- `#include <QList>`.
- `struct TabLocation { QString folder, image; bool operator==(const TabLocation&) const = default; }`.
- `inline constexpr int MaxTabs = 8`.
- `QList<TabLocation> tabs; int activeTab{};` appended, defaulted, to both `ResumeState` and `CheckpointInput`.
- No `readTabs` and no `<QJsonObject>` in the public header.

No other shared header changed.

### Fail-without-fix (mutation) checks

Each mutation was one edit to `src/app/viewer_controller.cpp` in a scratch copy of `e0adf16`. After each edit only `tabs_recovery_tests` was rebuilt, and the whole executable ran. The scratch copy also contained the two probe tests, so 53 functions and rows ran instead of 51.

| Mutation | Result |
| --- | --- |
| M1: remove `settleOn(target)` in `activateTab` | 49 passed, 4 failed: `closingDuringATabSwitchKeepsEveryTabsLocation`, `switchingToAnUndecodableTabKeepsEveryTabsLocation`, the probe for the unavailable folder (no in-flight decode), and the B1 probe |
| M2: remove `settleOn(target)` in `closeTab` | 48 passed, 5 failed: `closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation` (all three rows), `closingTheActiveOfTwoTabsBeforeTheOtherDecodesKeepsTheOtherLocation`, and the B1 probe (unrelated to the mutation) |
| M3: `activeLocation()` always returns `liveLocation()` | Not run. The build-slot queue was held behind three queued gate runs. The tests that would cover it are the second action before a decode (`newTab`, `tabs()` or `closeTab` mid-switch), and none of the listed tests targets that directly. |

The High-finding tests fail without their fix and pass with it. The legacy-validation fail-before evidence is the implementer's (handoff, 14 rows); this review did not re-run it.

## Windows test run (scratch merge)

### Merge

`git -C .worktrees\review-f015 merge --no-edit origin/claude/stack-f040-pinned` merged cleanly with the `ort` strategy and had no conflicts.

- **F040 since `b13f6c1`:** `include/glancelot/viewer_controller.hpp`, `src/app/display_manager.cpp`, `src/render/image_canvas.{cpp,hpp}`, `tests/reference_pin_tests.cpp`, `ui/Main.qml` and its handoff.
- **F015:** none of those files.

The two branches change no file in common.

### Build

`build-slot.ps1 -Run { … scripts\build.ps1 -BuildDir build-review-f015 }`: 589/589 targets built, exit 0.

### Targeted ctest (inside `gate-lock.ps1 -Branch review/f015`)

```
1/7 Test #11: window_slots .....................   Passed   29.54 sec
2/7 Test #38: recovery .........................   Passed   14.44 sec
3/7 Test #39: recovery_startup .................   Passed    0.74 sec
4/7 Test #40: recovery_controller ..............   Passed   14.69 sec
5/7 Test #41: recovery_process .................   Passed   27.70 sec
6/7 Test #42: tabs_recovery ....................   Passed   18.78 sec
7/7 Test #45: reference_pin ....................   Passed   18.24 sec
100% tests passed out of 7
Total Test time (real) = 124.42 sec
tabs_recovery: Totals: 51 passed, 0 failed, 0 skipped, 0 blacklisted, 18706ms
reference_pin: Totals: 10 passed, 0 failed, 2 skipped (nativeTextureReleasedOnClose/OnShed: need a native RHI backend; offscreen has none)
```

The full suite was not run. Only this targeted set was required, and the authoritative gate decides on the integrated commit.

## Unverified

- The B1 fix is not written. The transient form of B1 (target folder lists, destructor before its navigation result) was reasoned about but not tested.
- Physical, multi-window and GPU paths were not exercised; `reference_pin` skips two native-RHI cases offscreen.
