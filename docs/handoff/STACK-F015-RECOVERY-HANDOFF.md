# STACK-F015-RECOVERY handoff

Track session S5 (stack viewer), Phase 0 cycle 1. Review round 1 fixes by fix session S8 are in the section of that name at the end; where it and the earlier sections disagree, it is current. Contract: LEAD-WRAPUP-2026-10-22.md (F015 bullet), LEAD-TAKEOVER-2026-10-22.md section 4 and LEAD-PLAN-2026-10-22.md section 5. Not self-approved; a fresh reviewer decides.

| | |
| --- | --- |
| Branch | `claude/stack-f015-recovery`, stacked on `claude/stack-f040-pinned` |
| Base for review | `b13f6c1`: the F040 implementation tip, itself on `origin/main` `afeac1a` |
| Implementation tip | `496e82f` (review diff: `git diff b13f6c1 496e82f`; this handoff is the commit after it) |
| Commits | `297cf35` fix(f015), `496e82f` cmake: provisional target for STACK-F015-RECOVERY |

The F040 branch also has a later handoff-only commit, `974c42e`. It is not in this branch's history and does not touch any F015 file.

## What changed

Tabs now persist through the production recovery session (`recovery::Session` → `checkpoint()`), not through a second settings writer. When a claimed slot has a session, tab state only reaches disk in the checkpoint transaction.

- **Appended, defaulted fields.** `CheckpointInput` and `ResumeState` gain value-owned `QList<TabLocation> tabs` and `int activeTab`, with `TabLocation{folder,image}` and `MaxTabs = 8`. Existing `{folder, image, sequence}` call sites keep their meaning: a three-field input saves one location.
- **Atomic write.** `checkpoint()` writes `windows.<slot>.tabs` and `activeTab` in the same `settings_detail::transact` as `folder`, `image`, `saved`, and the recovery record's `checkpointRevision` and `sequence`. The existing claim and monotonic-sequence checks run first. Fewer than two records removes any stored tabs. Unknown fields in the slot's entry and every other slot are preserved, as before.
- **Coherent by construction.** `tabs[activeTab]` must equal the checkpoint's `{folder, image}`. Input breaking that, or with more than 8 records, an out-of-range active index, or a relative, NUL-containing or over-32768-character path, is refused with `Code::Corrupt` and nothing is written.
- **Read.** `beginWindow()` (through `position()`) and the session's read-only fallback (`readOnlyPosition()`) call the new `readTabs()`. Tab state is used only when it is a 2..8 array of `{folder: string, image: string}` with valid paths, an integral in-range `activeTab`, and an active record equal to the slot's position. Anything else, including a legacy entry without tabs, reads as one location. A position restored offline (`OwnedSlotScope{Position}`, which owns only folder/image/saved) therefore drops stale tabs instead of restoring the wrong set. Bad tab state never fails the begin. The next checkpoint replaces it.
- **Latest pending checkpoint.** `Session::accept` already keeps only the newest immutable `CheckpointInput`, and the tab snapshot travels inside it.
- **Controller (tab-persistence regions only).**
  - About lines 181-193: `TabRecord` is now `recovery::TabLocation`, plus `checkpointInput(sequence)`. Its active tab is the saved position itself, so the snapshot stays coherent while a tab switch is still decoding.
  - About line 440: `writePosition()` passes `checkpointInput(...)` to `recoverySession->accept`.
  - About lines 1231-1255: `restoreWindow()` takes tabs from `recoveryResume` on the session path. The legacy no-session entry is read exactly as before.
  - About lines 1486 and 1502: `activateTab()`, and `closeTab()` of the active tab, still mark the position dirty but stop the save timer. Their own navigation notes the settled position and restarts it, so a slow decode cannot persist a half-switched tab set. *(Superseded in review round 1: this left a checkpoint taken before the decode writing the previous tab's location into the target's slot. See the end of this file.)* `newTab()` and closing an inactive tab save as before. The final close (`writePosition(true)`) accepts the newest snapshot before `close(…, allowClean)`, so the session never cleans before it persists.
- **SAFE and reset.** Tabs are applied only inside `restoreWindow()`, which returns early under SAFE mode and when an explicit file was opened. After `--reset-settings` / `resetSettings()` there is no stored entry to restore. `--no-resume` has no claim.

### Not changed, deliberately

The no-claim path (`setWindowSlot(n)` without a recovery session, used by `window_slot_tests`) still writes `tabs` / `activeTab` through its existing `updateSettings` position writer. In that mode it is the only writer, so it is not a second writer next to the session. `window_slots` passes unchanged. Removing it would break `tests/window_slot_tests.cpp`, which is outside this packet's files. **Lead decision:** keep it, or retire the no-claim position writer in a later packet.

### Files

| File | Change |
| --- | --- |
| `include/glancelot/recovery_checkpoint.hpp` | Shared header. The exact diff is below. |
| `src/app/recovery_checkpoint.cpp` | `validTabs`, `readTabs`, tab write in `checkpoint()`, read in `position()` |
| `src/app/recovery_session.cpp` | `readOnlyPosition()` reads tabs (one line) |
| `src/app/viewer_controller.cpp` | Tab-persistence regions listed above, nothing else |
| `tests/tabs_recovery_tests.cpp` | New |
| `cmake/TabsRecoveryTests.cmake`, `CMakeLists.txt` (one `include` after `include(cmake/RecoveryTests.cmake)`) | Provisional target |

### Exact shared-header diff (round 0; superseded)

The final diff, without `readTabs`, is in "Review round 1 fixes" below.

```diff
--- a/include/glancelot/recovery_checkpoint.hpp
+++ b/include/glancelot/recovery_checkpoint.hpp
@@ -1,10 +1,22 @@
 #pragma once
 #include "settings_recovery.hpp"
+#include <QJsonObject>
+#include <QList>
 
 namespace glancelot::recovery {
+// F015 tabs: one saved folder/image location per tab, owned by value.
+struct TabLocation {
+    QString folder, image;
+    bool operator==(const TabLocation&) const = default;
+};
+inline constexpr int MaxTabs = 8;
 struct ResumeState {
     bool hasPosition{}, priorDirty{};
     QString folder, image, checkpointRevision;
+    // Appended, defaulted: empty for a single location (an entry without tabs, or tab state that is malformed,
+    // oversized or disagrees with folder/image). Otherwise 2..MaxTabs records and tabs[activeTab] == {folder, image}.
+    QList<TabLocation> tabs;
+    int activeTab{};
 };
 struct BeginResult {
     Status status;
@@ -13,6 +25,10 @@ struct BeginResult {
 struct CheckpointInput {
     QString folder, image;
     quint64 sequence{}; // positive, monotonic within one claim; stale commits refuse
+    // Appended, defaulted: fewer than two records save one location and remove stored tabs. Otherwise at most MaxTabs
+    // records with tabs[activeTab] == {folder, image}, written in the same transaction as folder/image and the revision.
+    QList<TabLocation> tabs;
+    int activeTab{};
 };
 struct CheckpointResult {
     Status status;
@@ -29,4 +45,6 @@ CheckpointResult checkpoint(const WindowClaim&, const CheckpointInput&, Options
 // Caller serializes one active/one latest pending operation with value ownership.
 Status finishWindow(const WindowClaim&, const QString& expectedCheckpointRevision,
                     quint64 expectedSequence, Options = {});
+// Reads a slot's stored tabs into `state` (whose folder/image are already set); invalid tab state leaves one location.
+void readTabs(const QJsonObject& windowRow, ResumeState& state);
 }
```

The settings policy (`settings_policy.cpp`) is unchanged. `tabs` and `activeTab` are unknown fields of the Position section, so they are preserved by every policy merge and do not change `policy::revision()`.

## Requested CMake target (S1 / lead)

`cmake/TabsRecoveryTests.cmake`, included right after `include(cmake/RecoveryTests.cmake)` inside the `BUILD_TESTING` block:

- Target `tabs_recovery_tests`. Sources: `tests/tabs_recovery_tests.cpp`, `src/app/viewer_controller.cpp`, `src/app/directory_entry_model.cpp`, `${glancelot_recovery_provider_sources}` and `${glancelot_recovery_implementation_sources}`.
- `GLANCELOT_RECOVERY_TESTING` (transaction fault hooks), includes `include src/app`, links `glancelot_core glancelot_platform Qt6::Gui Qt6::Test Qt6::Concurrent Threads::Threads`, theme-catalog resource, depends on `glancelot-decoder`.
- It compiles its own engine like `recovery_controller_tests`, so it sets `GLANCELOT_RECOVERY_ATTACHED TRUE`. Otherwise `glancelot_attach_recovery_consumers()` would also link `glancelot_recovery_common`.
- Test `tabs_recovery`. Environment `QT_QPA_PLATFORM=offscreen`, `TIMEOUT 120` (it takes about 7 s). Suggested EN-03 label: `fast`.

## Tests

`tests/tabs_recovery_tests.cpp` uses generated paths and generated PNGs in temporary config directories.

| Contract case | Test |
| --- | --- |
| Restart | `restartRestoresTabsThroughTheRecoverySession`: a real `WindowSlot` claim plus `ViewerController` with three tabs. After normal close the record is `clean`. A new claim and `restoreWindow()` bring back three tabs, active index 1, the active image, and switching works and is saved. `tabsShareTheCheckpointRevisionAndReturnOnTheNextClaim`: the API-level round trip after a dirty exit, the same revision, and removal on a one-location checkpoint. |
| Old entry | `legacyEntryWithoutTabsIsOneLocation`: unknown fields are kept and a three-field input still works. |
| Malformed / oversized | `malformedOrOversizedStoredTabsReadAsOneLocation` (14 data rows: non-array, one record, nine records, active out of range / negative / fractional / text / missing / disagreeing, relative path, NUL, a path over 32768 characters, non-object record, missing image). Each begins `Committed` with one location, and the next checkpoint replaces the bad state. `invalidTabInputIsRefusedWithoutWriting`: bytes unchanged, eight accepted. |
| Stale sequence | `staleSequenceCannotReplaceNewerTabs`: lower and equal sequences are refused byte-for-byte, and a stale `finishWindow` is refused. |
| Busy / failure / retry | `failedTabCheckpointNeverCleansAndRetrySucceeds`: WriteFailed, close returns false, the record stays dirty with bytes unchanged, then a retry persists and cleans. `busyWriterIsRetriedAndLatestPendingTabsWin`: settings lock held for 150 ms, the newest of three pending snapshots is written, a stale accept is refused, and the session cleans. `failedBeginOffersReadOnlyTabsWithoutWriting`: begin fails, read-only resume carries validated tabs, and nothing is written. `tabChangeWithoutNavigationIsSavedBeforeTheSessionCleans`: `newTab()` and an immediate close leave the record `clean` with two tabs persisted. |
| Other-slot preservation | `otherSlotsAndUnknownFieldsArePreserved`: slot 2's tab set and unknown fields, root unknowns and slot 1's unknowns are all kept. |
| SAFE / reset | `safeModeDoesNotRestoreTabs`: no navigation, no tabs, stored set left for a normal start. `resetSettingsDoesNotRestoreTabs`. Also `explicitFileWinsOverSavedTabs`. |

### Environment

As for F040: Ubuntu 24.04 cloud container, GCC 13.3, Ninja, Release. Qt 6.8.3 came from conda-forge because `download.qt.io` is blocked by this session's network policy. Build directory `.worktrees/claude-stack-f015-recovery/build-claude-stack-f015-recovery`.

### Targeted run

`tabs_recovery_tests`, run directly twice after the final source change:

```
Totals: 29 passed, 0 failed, 0 skipped, 0 blacklisted, 6842ms
Totals: 29 passed, 0 failed, 0 skipped, 0 blacklisted, 6749ms
```

The 29 are initTestCase, cleanupTestCase, 13 functions and 14 data rows of the malformed case.

Targeted ctest of the recovery set before the `failedBegin` case was added:

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f015-recovery -R "^(tabs_recovery|recovery|recovery_controller|recovery_startup|recovery_process|window_slots|reference_pin)$"
1/7 Test #11: window_slots .....................   Passed   15.11 sec
2/7 Test #38: recovery .........................   Passed   10.78 sec
3/7 Test #39: recovery_startup .................   Passed    0.20 sec
4/7 Test #40: recovery_controller ..............   Passed   12.34 sec
5/7 Test #41: recovery_process .................   Passed   23.90 sec
6/7 Test #42: tabs_recovery ....................   Passed    5.92 sec
7/7 Test #45: reference_pin ....................   Passed    5.47 sec
100% tests passed, 0 tests failed out of 7
Total Test time (real) =  73.72 sec
```

### Full offscreen suite (once, at `496e82f`)

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f015-recovery --output-on-failure
...
11/45 Test #11: window_slots .....................   Passed   15.62 sec
12/45 Test #12: folder_grid ......................***Failed  156.12 sec
13/45 Test #13: input_ui .........................***Failed   68.25 sec
16/45 Test #16: directory_model ..................***Failed    6.62 sec
23/45 Test #23: fs_watch .........................   Passed   70.28 sec
24/45 Test #24: fs_watch_budget ..................   Passed   19.34 sec
30/45 Test #30: cache ............................   Passed    2.28 sec
38/45 Test #38: recovery .........................   Passed   11.11 sec
39/45 Test #39: recovery_startup .................   Passed    0.20 sec
40/45 Test #40: recovery_controller ..............   Passed   12.35 sec
41/45 Test #41: recovery_process .................   Passed   24.02 sec
42/45 Test #42: tabs_recovery ....................   Passed    6.90 sec
43/45 Test #43: monitor_customization ............   Passed    6.59 sec
44/45 Test #44: monitor_customization_ui .........   Passed   15.89 sec
45/45 Test #45: reference_pin ....................   Passed    4.74 sec
93% tests passed, 3 tests failed out of 45
Total Test time (real) = 448.47 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
```

The other 30 tests passed. These are the same three single-case failures recorded in STACK-F040-PINNED-HANDOFF.md. They reproduce at the base `afeac1a` in this container: filesystem rename identity and an offscreen context menu. None touches recovery or tab code. The authoritative Windows gate decides.

## Unverified / notes

- Windows path forms: the tests compare paths with `QDir::cleanPath` and case-insensitive matching. Stored strings are the controller's `pendingFolder` / `pendingImage`, unchanged. Not run on Windows here.
- *(Round 0, superseded by round 1.)* A tab switch whose target fails to decode leaves the position dirty without a timer. The newest snapshot is saved at the next successful navigation or at close. If the process dies first, recovery offers the previous coherent checkpoint, and the record stays `dirty`, never falsely clean.
- `docs/WORK-QUEUE.yaml` was not touched (lead-owned).
- No UI files changed in this packet.

## Review round 1 fixes

Fix session S8, on `claude/stack-f015-recovery`. The original implementer was unavailable. Not self-approved; a new fresh reviewer decides.

| | |
| --- | --- |
| Base for review | `b13f6c1` (unchanged) |
| Round 0 tip | `f1c58f9` (this handoff), implementation `496e82f` |
| Round 1 fix commit | `0111e15` (round 1 only: `git diff f1c58f9 0111e15`; the whole packet: `git diff b13f6c1 0111e15`) |
| Branch tip | the commit that adds this section, directly after `0111e15` |

### 1. High: a checkpoint taken mid tab switch recorded the wrong tab locations

**Cause.** `activateTab()` set `activeTabIndex` to the target, while `pendingFolder`/`pendingImage` still held the previous tab's location until `notePosition()` ran after the asynchronous decode. `checkpointInput()` writes `tabs[activeTabIndex] = {pendingFolder, pendingImage}`. Any checkpoint in that window wrote the previous location into the target's slot: the destructor's `writePosition(true)`, a decode failure followed by close, `newTab()`, closing an inactive tab, or `retireRecovery()`. On the destructor path `accept` cleared `positionDirty`, so `close(1000, true)` marked that wrong record clean. Closing the active tab had the same window. It also saved the closed tab's location as the slot's position.

**Fix** (`src/app/viewer_controller.cpp`, tab-persistence regions):

- `Private::settleOn(target)` (about line 192) makes the target's stored location the pending position and sets `tabSettling`. `activateTab()` calls it for the target, and `closeTab()` calls it for the tab that becomes active when the active tab closes. From then on, `checkpointInput()` writes every tab's own location under its own index, with the selected tab active and its stored location as the slot's folder/image. The snapshot is coherent and complete at every moment, so the destructor's clean marker always covers a correct record. When closing leaves one tab, the single location is the remaining tab's, never the closed one's.
- The flag clears when the target settles: `notePosition()`, or a folder visit with no images (one token added at about line 1040, next to `pendingImage.clear()`). It stays set if the target fails to decode or its folder is unavailable. The active tab then keeps its stored location, which is the location the user selected.
- While a switch settles, live state (`path`, `nav`) may still describe the previous tab. A directory switch keeps the old `path` until the navigation result. So `Private::activeLocation()` returns the stored location instead of `liveLocation()` while `tabSettling` is set. `newTab()`, `activateTab()`, `closeTab()` and `tabs()` use it, so a second tab action before the decode also keeps every tab's location in memory.
- `activateTab()` and `closeTab()` of the active tab start the save timer again, as `newTab()` does. The snapshot no longer depends on the decode, and a crash during a slow decode recovers the switched set.
- `restoreWindow()` also calls `settleOn({folder, image})`. Until the restore lands, a new tab copies the saved location instead of an empty one, and a checkpoint keeps the saved location.

**Which active tab is recorded.** The record says the selected tab is active and gives its stored location as the slot's position. That location was settled when the tab was last active, or was restored from a checkpoint. A restart therefore reopens the tab the user selected, at the location that tab held, which is what the live window shows. The reviewer's alternative, keeping the previous tab active until the target settles, was not chosen. Under it, closing the active tab would have had no settled tab to fall back on.

### 2. Lead decision: `readTabs` is internal

`readTabs(const QJsonObject&, ResumeState&)` moved from `include/glancelot/recovery_checkpoint.hpp` to the new internal header `src/app/recovery_tabs.hpp`. `recovery_checkpoint.cpp`, `recovery_session.cpp`, `viewer_controller.cpp` and the test include it. `#include <QJsonObject>` left the public header with it. `TabLocation`, `MaxTabs` and the appended `tabs`/`activeTab` fields are unchanged. No CMake change was needed, because every consumer already has `src/app` on its include path.

### 3. Lead decision: the legacy no-claim restore validates like the claimed path

`restoreWindow()` without a recovery session (about lines 1247-1252) now fills a `ResumeState` from the entry and calls `recovery::readTabs()`. This is the same function the claimed begin and the read-only fallback use, so it checks the same things: 2..`MaxTabs` records, `{folder: string, image: string}` objects, valid absolute paths without NUL and within 32768 characters, an integral in-range `activeTab`, and `tabs[activeTab] == {folder, image}`. Anything else restores one location. The no-claim writer in `writePosition()` now builds its rows from `checkpointInput()`, so it saves the same coherent snapshot as the checkpoint.

### 4. Tests

- `failedBeginOffersReadOnlyTabsWithoutWriting`: the always-true `!accept || !begun` is gone. After the failed begin it offers a checkpoint, waits for the worker, and asserts `!begun`, `persistedSequence == 0`, a non-committed `lastAttempt` with no revision, and unchanged bytes, both before and after `close`.
- `resetSettingsDoesNotRestoreTabs`: asserts the three tabs are stored before the reset and are present in the set-aside file. It then waits for the new claim's begin (the `dirty` marker) before checking, so the restore has really run. It also checks that the live file has no tabs.
- `tabsAndPositionLandInOneTransaction` (new): a failure at `BeforeReplace` leaves the bytes unchanged. On success, exactly one replace (`AfterReplace` observed once) moves tabs, `activeTab`, folder/image, `checkpointRevision` and `sequence` together. Just before the replace the file is entirely the earlier checkpoint; just after it, it is entirely the new one with the returned revision.
- `busyWriterIsRetriedAndLatestPendingTabsWin`: an observe hook, copied into the session's worker, records the recovery sequence after every replace. The test asserts sequences 1 and 2 never reached disk and 3 did. The accepts are 20 ms apart, inside a 250 ms lock hold (was 150 ms), so the worker is already retrying snapshot 1 when 2 and 3 replace it. Mutation check: with the session's "newer pending supersedes the retry" breaks removed from `recovery_session.cpp`, the test fails with `(0, 1, 3, 3)` (run twice). It passes on the real code (run three times). The mutation was reverted and is not in the commit.

### 5. `MaxTabs`

`newTab()` uses `recovery::MaxTabs` instead of the literal 8.

### New tests for item 1 and item 3

| Test | Covers |
| --- | --- |
| `closingDuringATabSwitchKeepsEveryTabsLocation` | Three tabs settled on gamma; `activateTab(0)`, then destroy before `imageReady`. Asserts tabs alpha, beta, gamma in their own slots, active 0, position alpha, record `clean`. |
| `closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation` (rows: middle, last, first of three) | Close the active tab, then destroy before the new active tab decodes. Asserts the remaining tabs, the selected index and its position, record `clean`. |
| `closingTheActiveOfTwoTabsBeforeTheOtherDecodesKeepsTheOtherLocation` | Close an inactive tab (saved), then the active one of two. Asserts one location, the remaining tab's, no tabs, record `clean`. |
| `switchingToAnUndecodableTabKeepsEveryTabsLocation` | Tab 1 is a generated corrupt `broken/x.png`. Switch to it; decode fails (`!loading`, `!imageReady`); destroy. Asserts tabs alpha, broken, gamma, active 1, record `clean`. |
| `legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath` (the same 14 malformed rows as the claimed test) | `setWindowSlot(1)` with no claim, `restoreWindow()`: one location. |
| `legacyNoClaimRestoreKeepsValidTabs` | A valid legacy set still restores (3 tabs, active 1). |

Each item 1 assertion reports the stored record on failure.

### Fail before, pass after

Fail before: the new tests built against the round 0 controller. At that point only the non-behavioural parts of round 1 were applied: the `readTabs` header move and `MaxTabs`. Temporary paths are shortened here.

```
FAIL!  : TabsRecoveryTests::closingDuringATabSwitchKeepsEveryTabsLocation() 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [gamma/c.png in gamma, beta/b.png in beta, gamma/c.png in gamma] active 0, position gamma/c.png in gamma)
FAIL!  : TabsRecoveryTests::closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation(middle of three) 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [alpha/a.png in alpha, beta/b.png in beta] active 1, position beta/b.png in beta)
FAIL!  : TabsRecoveryTests::closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation(last of three) 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [alpha/a.png in alpha, gamma/c.png in gamma] active 1, position gamma/c.png in gamma)
FAIL!  : TabsRecoveryTests::closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation(first of three) 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [alpha/a.png in alpha, gamma/c.png in gamma] active 0, position alpha/a.png in alpha)
FAIL!  : TabsRecoveryTests::closingTheActiveOfTwoTabsBeforeTheOtherDecodesKeepsTheOtherLocation() 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [] active , position gamma/c.png in gamma)
FAIL!  : TabsRecoveryTests::switchingToAnUndecodableTabKeepsEveryTabsLocation() 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [alpha/a.png in alpha, gamma/c.png in gamma, gamma/c.png in gamma] active 1, position gamma/c.png in gamma)
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active out of range) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active negative) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active fraction) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active text) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active missing) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(active disagrees with position) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(relative path) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(NUL in path) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(path too long) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(record not an object) 'viewer.tabs().isEmpty()' returned FALSE. ()
FAIL!  : TabsRecoveryTests::legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath(record missing image) 'viewer.tabs().isEmpty()' returned FALSE. ()
Totals: 34 passed, 17 failed, 0 skipped, 0 blacklisted, 13528ms
```

Each item 1 failure is the reported defect: the previous tab's location duplicated into the target's slot, or the closed tab's location kept as the position. The record was marked `clean` in each case; the `clean` assertion comes after the content check, so it is not reached. The legacy rows "not an array", "one record" and "oversized" already read as one location before the fix. The item 4 test changes are test strength only and pass on both versions; the busy test's ability to fail is shown by the mutation above.

Pass after: `tabs_recovery_tests`, run directly twice at `0111e15`:

```
Totals: 51 passed, 0 failed, 0 skipped, 0 blacklisted, 13493ms
Totals: 51 passed, 0 failed, 0 skipped, 0 blacklisted, 13549ms
```

51 = initTestCase, cleanupTestCase, 21 functions, and the data rows (14 malformed, 14 legacy, 3 close-active).

### Targeted ctest (at `0111e15`)

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f015-recovery -R "^(tabs_recovery|recovery|recovery_controller|recovery_startup|recovery_process|window_slots|reference_pin)$"
1/7 Test #11: window_slots .....................   Passed   14.94 sec
2/7 Test #38: recovery .........................   Passed   10.79 sec
3/7 Test #39: recovery_startup .................   Passed    0.18 sec
4/7 Test #40: recovery_controller ..............   Passed   12.16 sec
5/7 Test #41: recovery_process .................   Passed   23.29 sec
6/7 Test #42: tabs_recovery ....................   Passed   13.54 sec
7/7 Test #45: reference_pin ....................   Passed    4.48 sec
100% tests passed, 0 tests failed out of 7
Total Test time (real) =  79.38 sec
```

### Full offscreen suite (once)

This ran on the round 1 source before one test-only edit: the busy test's shared record changed from `std::pair<std::mutex, QStringList>` to a small struct, for MSVC. `tabs_recovery` and the targeted set above were re-run after that edit.

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f015-recovery --output-on-failure
11/45 Test #11: window_slots .....................   Passed   14.97 sec
12/45 Test #12: folder_grid ......................***Failed  159.33 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
13/45 Test #13: input_ui .........................***Failed   66.76 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
16/45 Test #16: directory_model ..................***Failed    5.86 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
23/45 Test #23: fs_watch .........................   Passed   70.30 sec
24/45 Test #24: fs_watch_budget ..................   Passed   20.11 sec
30/45 Test #30: cache ............................   Passed    2.01 sec
38/45 Test #38: recovery .........................   Passed   11.18 sec
39/45 Test #39: recovery_startup .................   Passed    0.16 sec
40/45 Test #40: recovery_controller ..............   Passed   12.20 sec
41/45 Test #41: recovery_process .................   Passed   23.56 sec
42/45 Test #42: tabs_recovery ....................   Passed   13.72 sec
43/45 Test #43: monitor_customization ............   Passed    6.64 sec
44/45 Test #44: monitor_customization_ui .........   Passed   15.65 sec
45/45 Test #45: reference_pin ....................   Passed    4.84 sec
93% tests passed, 3 tests failed out of 45
Total Test time (real) = 454.57 sec
```

The other 30 tests passed. The three failures are the documented baseline failures with conda-forge Qt 6.8.3 (PHASE0-CYCLE1, EN-05 handoff). They also failed in round 0, and none touches tab or recovery code.

### Environment

Ubuntu 24.04 cloud container, GCC 13.3, Ninja, Release. `aqt` failed with a proxy error on `download.qt.io` and its mirror. `micro.mamba.pm` was refused by the proxy too, so micromamba 2.3.0 came straight from `conda.anaconda.org/conda-forge/linux-64/micromamba-2.3.0-0.tar.bz2`. It then installed conda-forge `qt6-main=6.8.3` to `/opt/qt683`, as the PHASE0-CYCLE1 fallback describes. `libgl1-mesa-dev` came from apt. Build directory `build-claude-stack-f015-recovery`.

### Final exact shared-header diff (`git diff b13f6c1 0111e15 -- include/glancelot/recovery_checkpoint.hpp`)

```diff
--- a/include/glancelot/recovery_checkpoint.hpp
+++ b/include/glancelot/recovery_checkpoint.hpp
@@ -1,10 +1,21 @@
 #pragma once
 #include "settings_recovery.hpp"
+#include <QList>
 
 namespace glancelot::recovery {
+// F015 tabs: one saved folder/image location per tab, owned by value.
+struct TabLocation {
+    QString folder, image;
+    bool operator==(const TabLocation&) const = default;
+};
+inline constexpr int MaxTabs = 8;
 struct ResumeState {
     bool hasPosition{}, priorDirty{};
     QString folder, image, checkpointRevision;
+    // Appended, defaulted: empty for a single location (an entry without tabs, or tab state that is malformed,
+    // oversized or disagrees with folder/image). Otherwise 2..MaxTabs records and tabs[activeTab] == {folder, image}.
+    QList<TabLocation> tabs;
+    int activeTab{};
 };
 struct BeginResult {
     Status status;
@@ -13,6 +24,10 @@ struct BeginResult {
 struct CheckpointInput {
     QString folder, image;
     quint64 sequence{}; // positive, monotonic within one claim; stale commits refuse
+    // Appended, defaulted: fewer than two records save one location and remove stored tabs. Otherwise at most MaxTabs
+    // records with tabs[activeTab] == {folder, image}, written in the same transaction as folder/image and the revision.
+    QList<TabLocation> tabs;
+    int activeTab{};
 };
 struct CheckpointResult {
     Status status;
```

The new internal header, `src/app/recovery_tabs.hpp` (not shared):

```cpp
#pragma once
#include "glancelot/recovery_checkpoint.hpp"
#include <QJsonObject>

namespace glancelot::recovery {
// F015, internal: reads a slot's stored tabs into `state`, whose folder/image are already set. ...
void readTabs(const QJsonObject& windowRow, ResumeState& state);
}
```

### Files changed in round 1

| File | Change |
| --- | --- |
| `include/glancelot/recovery_checkpoint.hpp` | `readTabs` and `<QJsonObject>` removed |
| `src/app/recovery_tabs.hpp` | New internal header with `readTabs` |
| `src/app/recovery_checkpoint.cpp`, `src/app/recovery_session.cpp` | Include the internal header (one line each) |
| `src/app/viewer_controller.cpp` | Tab-persistence regions: `tabSettling`, `activeLocation()`, `settleOn()`; `notePosition()`; the no-claim writer in `writePosition()`; the folder-without-images settle (one token); `restoreWindow()`; `tabs()`, `newTab()`, `activateTab()`, `closeTab()` |
| `tests/tabs_recovery_tests.cpp` | Tests above |

No CMake, queue, UI or other shared files changed.

### Unverified / notes (round 1)

- Not run on Windows here. The `build.yml` `viewer` job on this push is the early MSVC signal.
- A slot or claim change in the middle of a tab switch (`setWindowSlot` / `setWindowClaim` replay `notePosition(lastDecodedPath)`) would settle the switch on the previous tab's last decoded image. Those functions are outside this packet's regions, and claims are set when the window is created, before tabs exist. No production path reaches this. It is not covered by a test.
- A decode that fails after a switch leaves the target settling, so the saved position stays the target's stored location. That is the image that failed. A restart retries it and reports the same decode error, as the live window does.
