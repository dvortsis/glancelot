# STACK-F015-RECOVERY handoff

Track session S5 (stack viewer), Phase 0 cycle 1. Review round 1, 2 and 3 fixes are in the sections of those names at the end; where they and the earlier sections disagree, the latest is current. Contract: LEAD-WRAPUP-2026-10-22.md (F015 bullet), LEAD-TAKEOVER-2026-10-22.md section 4 and LEAD-PLAN-2026-10-22.md section 5. Not self-approved; a fresh reviewer decides.

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

The no-claim path (`setWindowSlot(n)` without a recovery session, used by `window_slot_tests`) still writes `tabs` / `activeTab` through its existing `updateSettings` position writer. In that mode it is the only writer, so it is not a second writer next to the session. `window_slots` passes unchanged. Removing it would break `tests/window_slot_tests.cpp`, which is outside this packet's files. **Lead decision:** keep it, or retire the no-claim position writer in a later packet. *(Superseded in review round 2: the lead retired the no-claim tab write. See the end of this file.)*

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
- *(Corrected in review round 2: this note was wrong.)* A slot or claim change in the middle of a tab switch (`setWindowSlot` / `setWindowClaim` replay `notePosition(lastDecodedPath)`) would settle the switch on the previous tab's last decoded image. A production path does reach it: `app/main.cpp:211` retries a busy startup claim every 4 s, up to 8 times, and each retry calls `setWindowClaim` then `restoreWindow` on a live window that may already have tabs. Round 2 fixes the replay and tests it.
- A decode that fails after a switch leaves the target settling, so the saved position stays the target's stored location. That is the image that failed. A restart retries it and reports the same decode error, as the live window does.

## Review round 2 fixes

Fix session S9 (Claude Code, Opus 5.5, on the lead's Windows PC). Earlier implementers were unavailable. Not self-approved; a new fresh reviewer decides. Review: `origin/claude/review-stack-f015` `6d882b2`, `docs/verification/STACK-F015-RECOVERY-REVIEW.md` (CHANGES REQUIRED: B1; N1, N3).

| | |
| --- | --- |
| Round 1 tip | `8afe4cd` |
| F040 merge | `5396de7`: `git merge --no-edit origin/claude/stack-f040-pinned` (F040's approved tip `b610348`). Clean `ort` merge, no conflicts; the two branches change no file in common. F015 now stacks on F040's tip, not on its implementation commit `b13f6c1`. |
| Round 2 fix commit | the commit that adds this section (round 2 only: `git diff 5396de7 <tip>`; the whole packet on F040's tip: `git diff b610348 <tip>`) |

### B1 (High): a stale decode from the previous tab settled a folder-route switch

**Cause.** `activateTab()` and `closeTab()` of the active tab fall back to `navigateDirectory(target.folder)` when the target's image is gone. That route neither bumps `generation` nor cancels a decode of the previous tab still in flight. The decode landed, `notePosition(path)` cleared `tabSettling` and wrote the previous tab's image into `pendingFolder`/`pendingImage` while `activeTabIndex` was already the target. With the target folder unavailable nothing corrected it, and the destructor marked the wrong record `clean`.

**Fix (option B, generation-style fencing)**, `src/app/viewer_controller.cpp`:

- `Private::openSerial` / `settleSerial` (about line 190). `ViewerController::open()` increments `openSerial` next to its `++generation; cancel()` (about line 1617; one token outside the tab regions, declared here). `settleOn()` records `settleSerial=openSerial`.
- `notePosition()` (about line 447) clears `tabSettling` and moves the position only if `openSerial!=settleSerial`, that is, only for an image opened after the switch began. A decode issued before the switch still updates `lastDecodedPath` (the canvas shows it) but leaves the target's stored location as the position and the active tab's location.
- Why a separate counter and not `generation`: a late fs-watch baseline (`decodeStartedWithoutBaseline`, about line 375) runs `++generation` and re-decodes the *previous* tab's `path` during a folder-route switch, so a generation fence would let that re-decode settle the target. Only `open()` bumps `openSerial`, and every `open()` also bumps `generation` and cancels the older decode.
- The in-flight decode is not retired on switch (option A). With `++generation; cancel()` the failed-folder branch never clears `loading`, so the window would sit on "Opening…" until the 15 s deadline reported a false decoder timeout. The fence alone keeps the tab strip and the checkpoint correct.
- Transient form (the target folder lists images; the destructor runs before its navigation result): the stale decode is fenced the same way, so the destructor saves the target's stored location. This is reasoned from the same code path, not tested: the navigation worker and the decoder process cannot be ordered deterministically here.

### N1: the claim-retry replay (`app/main.cpp:211`)

The round 1 note said no production path reaches the replay mid-switch; it does (corrected above). `setWindowSlot()` (two sites) and `setWindowClaim()` now call `Private::replayPosition()` (about line 203) instead of `notePosition(lastDecodedPath)`. Outside a switch it is the old replay. During a switch it keeps the target's stored location as the position and marks it dirty (`positionDirty=slot>0`, timer started), so the new claim's session checkpoints the correct tab set and the destructor never cleans a record it did not write. The fence alone does not cover this: on the image route `open(target)` has already advanced `openSerial`, so the old replay would settle on the previous tab's last decoded image. `restoreWindow()` after the retry returns early as before (a path is open or a tab action set `explicitPositionIntent`).

### N3 (lead decision): no-claim tab write retired

Production always claims a slot (`app/main.cpp:206` calls `setWindowClaim`; the retry at line 211 too), so tab state is written only by the recovery checkpoint. The no-claim branch of `writePosition()` (about line 470) no longer builds tab rows: it writes `folder`/`image`/`saved` only, and replacing the slot entry drops any stale stored tabs. Reading is unchanged: the legacy no-claim restore still validates through `recovery::readTabs`, the same function as the claimed path (`legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath`, 14 rows; `legacyNoClaimRestoreKeepsValidTabs`).

Scope as implemented: the folder/image no-claim position writer itself stays. `tests/window_slot_tests.cpp` (the required `window_slots` target, 60+ uses) and `tests/folder_tree_tests.cpp:685` depend on it, and both are outside this packet's files. If the lead wants the whole no-claim writer retired, that is a follow-up packet that also owns those tests.

**One edit outside this packet's test file.** Retiring the tab write made `window_slots` fail deterministically (not budget flake) in `WindowSlotTests::tabsKeepLocationsAndRestoreWithTheWindow`, which asserted the no-claim tab round trip:

```
FAIL!  : WindowSlotTests::tabsKeepLocationsAndRestoreWithTheWindow() Compared values are not the same
   Actual   (w.tabs().size()): 0
C:\src\Github\Glancelot\.worktrees\claude-f015\tests\window_slot_tests.cpp(1002) : failure location
Totals: 37 passed, 1 failed, 0 skipped, 0 blacklisted, 31135ms
```

That function is F015's own (`2566014` "F015: tabs as saved locations (shape A) …"), and the lead decision makes its persistence half obsolete. It was ported, not weakened: each of its two windows now claims a slot with `WindowSlot(config, 0)` declared before the controller (so the claim outlives the final checkpoint) and calls `setWindowClaim()` instead of `setWindowSlot(6)`. Every assertion is unchanged (implicit single tab, titles, switching, the eight-tab cap, closing down to two, the restored set and active tab, closing down to one). After the port: `window_slot_tests.exe` `Totals: 38 passed, 0 failed, 0 skipped, 0 blacklisted, 52071ms`. No other test in `tests/` persists tabs without a claim (`input_ui_tests.cpp:785` uses `tab-new` for the UI strip only). If `tests/window_slot_tests.cpp` belongs to another track, the lead may reassign this edit.

### Tests added (`tests/tabs_recovery_tests.cpp`)

Each asserts the live tab strip (`viewer.tabs()`, `activeTab()`) before the window closes, then every tab's stored location, the active index and the slot position, and that the record marked `clean` is that correct record. A precondition in each makes it unable to pass vacuously (the stale decode really landed, the folder navigation really was pending, the target really had not decoded).

| Test | Covers |
| --- | --- |
| `staleDecodeCannotSettleADirectorySwitch` (rows: switch, close the active tab) | The reviewer's scenario. Three tabs settled on gamma; beta's folder removed; `open(alpha)` in tab 2, still loading; then `activateTab(1)` or `closeTab(2)`. Waits until the folder route failed and the stale decode landed (`shows(alpha)`). Expects `[alpha, beta, alpha]` active 1, or `[alpha, beta]` active 1. |
| `secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation` (rows: new tab, switch again, close another tab, stale decode + new tab) | A second tab action while the folder-route switch is still pending (`directoryPending()` asserted). Covers the settling branch of `activeLocation()` (the reviewer's M3 gap). Expects `[alpha, beta, beta, gamma]` active 2; `[alpha, beta, gamma]` active 0; `[beta, gamma]` active 0; `[alpha, beta, beta, alpha]` active 2. |
| `claimRetryDuringATabSwitchKeepsEveryTabsLocation` | N1. An unclaimed window (the busy startup case) with three tabs; `activateTab(0)` (image route, not decoded); then `WindowSlot` + `setWindowClaim()` + `restoreWindow()` as `app/main.cpp:211` does; destroy. Expects `[alpha, beta, gamma]` active 0, record `clean`. |
| `noClaimWriterNeverWritesTabs` | N3. A stored valid tab set; an unclaimed `setWindowSlot(1)` window with two live tabs; `flushWindowPosition()` and close. The position is saved; the entry has no `tabs` and no `activeTab`; no recovery record exists. |

### Fail before, pass after

Fail before: the tests above built against the round 1 controller as merged (`5396de7`, controller unmodified). `tabs_recovery_tests.exe` with the four new functions, Windows, offscreen. Temporary paths are shortened to the media-relative part.

```
FAIL!  : TabsRecoveryTests::staleDecodeCannotSettleADirectorySwitch(switch) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1)
FAIL!  : TabsRecoveryTests::staleDecodeCannotSettleADirectorySwitch(close the active tab) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png] active 1)
PASS   : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(new tab)
PASS   : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(switch again)
PASS   : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(close another tab)
FAIL!  : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(stale decode, new tab) 'live.isEmpty()' returned FALSE. (live tab 2 holds the wrong location: live tabs [alpha/a.png, beta/b.png, alpha/a.png, alpha/a.png] active 2)
FAIL!  : TabsRecoveryTests::claimRetryDuringATabSwitchKeepsEveryTabsLocation() 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [gamma/c.png in gamma, beta/b.png in beta, gamma/c.png in gamma] active 0, position gamma/c.png in gamma)
FAIL!  : TabsRecoveryTests::noClaimWriterNeverWritesTabs() '!position(config.path()).contains("tabs") && !position(config.path()).contains("activeTab")' returned FALSE. ({"activeTab":1,"folder":".../beta","image":".../beta/b.png","saved":1792744115,"tabs":[{"folder":".../alpha","image":".../alpha/a.png"},{"folder":".../beta","image":".../beta/b.png"}]})
Totals: 5 passed, 5 failed, 0 skipped, 0 blacklisted, 7978ms
```

The B1 rows fail on the live tab strip first: all tabs read `alpha`, so beta's location is gone in memory as well as on disk (the reviewer's run showed the same on disk, `[alpha, alpha, alpha]`). The three non-stale rows of the second-action test pass on round 1 by design: they cover round 1's settling branch of `activeLocation()`, and their ability to fail is shown by M3 below.

Pass after (fix applied, whole executable):

```
Totals: 59 passed, 0 failed, 0 skipped, 0 blacklisted, 38154ms
```

59 = initTestCase, cleanupTestCase, 25 functions, and the data rows (14 malformed, 14 legacy, 3 close-active, 2 stale-decode, 4 second-action).

### Mutation checks (each one edit, rebuilt `tabs_recovery_tests` only, then reverted)

| Mutation | Result |
| --- | --- |
| M3 (the reviewer could not run it): `activeLocation()` always returns `liveLocation()`, removing the "where am I" fallback during a switch | 53 passed, 6 failed: both `staleDecodeCannotSettleADirectorySwitch` rows and all four `secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation` rows. Output below. |
| M4: `setWindowClaim()` goes back to the old replay (`tabSettling=false; notePosition(lastDecodedPath)`), with the B1 fence kept | `claimRetryDuringATabSwitchKeepsEveryTabsLocation` fails: stored `[gamma, beta, gamma]` active 0, position gamma. Shows the replay guard is needed on top of the fence. |

```
M3:
FAIL!  : TabsRecoveryTests::staleDecodeCannotSettleADirectorySwitch(switch) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1)
FAIL!  : TabsRecoveryTests::staleDecodeCannotSettleADirectorySwitch(close the active tab) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png] active 1)
FAIL!  : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(new tab) 'early.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, gamma/c.png, gamma/c.png, gamma/c.png] active 2)
FAIL!  : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(switch again) 'early.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 0)
FAIL!  : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(close another tab) 'early.isEmpty()' returned FALSE. (live tab 0 holds the wrong location: live tabs [gamma/c.png, gamma/c.png] active 0)
FAIL!  : TabsRecoveryTests::secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation(stale decode, new tab) 'early.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png, alpha/a.png, alpha/a.png] active 2)
Totals: 53 passed, 6 failed, 0 skipped, 0 blacklisted, 33387ms

M4:
FAIL!  : TabsRecoveryTests::claimRetryDuringATabSwitchKeepsEveryTabsLocation() 'differ.isEmpty()' returned FALSE. (wrong position: stored tabs [gamma/c.png in gamma, beta/b.png in beta, gamma/c.png in gamma] active 0, position gamma/c.png in gamma)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 865ms
```

### Build and targeted ctest

Environment: Windows 11, MSVC 2022, Qt 6.8.3 (`.tools`), Ninja Release, offscreen. Build directory `build-claude-f015`.

Build, after the mutations were reverted: `build-slot.ps1 -Run { … scripts\build.ps1 -BuildDir build-claude-f015 }`, 554/554 targets, exit 0. `window_slot_tests` was then rebuilt alone for the port described under N3.

First gated run, before the `window_slots` port (both runs the same, deterministic): `window_slots` failed, the other six passed. The failure is the one quoted under N3.

```
1/7 Test #11: window_slots .....................***Failed   29.01 sec
...
86% tests passed, 1 tests failed out of 7
```

Final: `gate-lock.ps1 -Branch claude/f015-fix -Run { … ctest -R '^(tabs_recovery|recovery|recovery_controller|recovery_startup|recovery_process|window_slots|reference_pin)$' --output-on-failure }`, twice, with PATH prefixed by `.tools\python\Scripts` and the Qt `bin`:

```
=== run 1
1/7 Test #11: window_slots .....................   Passed   28.63 sec
2/7 Test #38: recovery .........................   Passed   14.21 sec
3/7 Test #39: recovery_startup .................   Passed    0.48 sec
4/7 Test #40: recovery_controller ..............   Passed   14.46 sec
5/7 Test #41: recovery_process .................   Passed   28.90 sec
6/7 Test #42: tabs_recovery ....................   Passed   27.81 sec
7/7 Test #45: reference_pin ....................   Passed   16.42 sec
100% tests passed out of 7
Total Test time (real) = 130.96 sec
exit 0
=== run 2
1/7 Test #11: window_slots .....................   Passed   27.23 sec
2/7 Test #38: recovery .........................   Passed   13.73 sec
3/7 Test #39: recovery_startup .................   Passed    0.50 sec
4/7 Test #40: recovery_controller ..............   Passed   14.33 sec
5/7 Test #41: recovery_process .................   Passed   28.25 sec
6/7 Test #42: tabs_recovery ....................   Passed   27.51 sec
7/7 Test #45: reference_pin ....................   Passed   16.28 sec
100% tests passed out of 7
Total Test time (real) = 127.84 sec
exit 0
```

### Files changed in round 2

| File | Change |
| --- | --- |
| `src/app/viewer_controller.cpp` | Tab-persistence regions: `openSerial`/`settleSerial`, `settleOn()`, `replayPosition()`, the fence in `notePosition()`, the no-claim writer without tabs. Outside them, declared: one `++d->openSerial` in `open()`, and the replay call in `setWindowSlot()` (two sites) and `setWindowClaim()`. |
| `tests/tabs_recovery_tests.cpp` | `showThreeTabs`, `liveTabsDiffer`, `pick` helpers and the four tests above |
| `tests/window_slot_tests.cpp` | `tabsKeepLocationsAndRestoreWithTheWindow` only (F015's own test, `2566014`): both windows claim a slot instead of `setWindowSlot(6)`; assertions unchanged (see N3) |
| `docs/handoff/STACK-F015-RECOVERY-HANDOFF.md` | This section; the round 1 replay note and the round 0 no-claim note marked corrected/superseded |

No shared header, CMake, queue or UI file changed in round 2. The public header diff is unchanged from round 1.

### Pre-handoff checklist (AGENTS.md)

- Branch `claude/stack-f015-recovery`; tip SHA in the table above and in the commit; pushed fast-forward.
- Files changed: listed above.
- Actual `ctest` output: targeted `ctest -R` twice above. The full offscreen suite was not run in round 2; the authoritative `lead-windows-gate` decides on the integrated commit.
- Failure and cancellation cases: folder unavailable during a switch and a close; a stale decode landing after the switch; a second tab action before settling; a claim retry mid-switch; no-claim writes; plus round 1's decode failure, write failure, busy writer and stale sequence cases.
- Unverified: the transient B1 form (target folder lists; destructor before its navigation result), reasoned only. Physical, multi-window and GPU paths not exercised; `reference_pin` skips its native-RHI cases offscreen. No UI files changed, so the ui-design-skills did not apply.
- Lead-owned files: none touched in round 2. N2 (the provisional `CMakeLists.txt` include and `cmake/TabsRecoveryTests.cmake`) is still the lead's call.

## Review round 3 fixes

Fix session S9 again (Claude Code, Opus 5.5, on the lead's Windows PC). Not self-approved; a new fresh reviewer decides. Review: `origin/claude/review-stack-f015-r2` `237b070`, `docs/verification/STACK-F015-RECOVERY-REVIEW-R2.md` (CHANGES REQUIRED: B2; B1 closed).

| | |
| --- | --- |
| Round 2 tip | `9c8d952` |
| Integration merge | `97fb2ff`: `git merge --no-edit origin/main` (main `b9dc0ec`). One conflict, `CMakeLists.txt`: F040's `include(cmake/ReferencePinTests.cmake)` and EN-06's `CommandRoutingTests` block at the same spot; both kept. `src/app/viewer_controller.cpp` and `ui/Main.qml` auto-merged. |
| CMake (lead's one-time grant) | `f91a9db`, plus one more line in the round 3 commit (below) |
| Round 3 fix commit | the commit that adds this section (round 3 only: `git diff f91a9db <tip>`) |

### CMake under the lead's one-time grant

- `cmake/TabsRecoveryTests.cmake`: the hand-listed `viewer_controller.cpp` replaced by `${GLANCELOT_VIEWER_SOURCES}` (EN-05; fixes the `DecodeCache` link error the reviewer saw on the merged tree). `glancelot_test_labels(tabs_recovery LABELS process)`.
- `cmake/ReferencePinTests.cmake`: the hand-listed `viewer_controller.cpp` and `settings_file.cpp` replaced by `${GLANCELOT_VIEWER_SOURCES}`. `glancelot_test_labels(reference_pin LABELS ui process)` (one declaration; F040 does not add another on this branch).
- **One addition beyond the brief, same file:** `reference_pin` also needs `${GLANCELOT_VIEWER_CONTEXT_SOURCES}`. Since EN-04, `tests/ui_test_support.hpp`'s `Harness` installs `FavoritesModel` and `FolderHistory`, so with the viewer list alone the merged tree failed to link:

  ```
  reference_pin_tests.cpp.obj : error LNK2019: unresolved external symbol "public: __cdecl FavoritesModel::FavoritesModel(class QObject *)" ...
  reference_pin_tests.cpp.obj : error LNK2019: unresolved external symbol "public: __cdecl FolderHistory::FolderHistory(class QObject *)" ...
  reference_pin_tests.exe : fatal error LNK1120: 4 unresolved externals
  ```

  `cmake/ViewerSources.cmake` documents exactly this list for targets that load `ui/Main.qml` through that helper.

### B2 (High): a reopen of the previous tab's content settled a failed folder-route switch

**Cause** (reviewer's analysis, confirmed). After a folder-route switch to an unavailable folder, `path`, the canvas and the folder watch stayed on the previous tab's image. F5, a folder-watcher change (`reconcileObservation`), `reconcileRenamedPath`, or an external delete (a folder visit through the no-images branch) reopened or cleared that content. Round 2's fence counted every `open()`, and the no-images branch settled unconditionally, so the target took the previous tab's location and the destructor saved it `clean`.

**Fix, following the lead decision** (`src/app/viewer_controller.cpp`):

(a) *A tab's location changes only from its own folder or the user's explicit navigation.*

- `settleOn()` now also records `settleFolder` (the target's folder) and clears `settleAny`, `tabUnavailable` and `settleNavigation`.
- `notePosition()` settles only when the image was opened after the switch (round 2's `openSerial` fence) **and** `settlesIn(folder)`: the image's folder equals `settleFolder` by `fs_watch::key` (clean path, case-folded on Windows), or `settleAny` is set.
- The folder-visit branch with no images settles only if `settlesIn(visited)`. A reconcile's visit of the previous tab's folder never settles.
- `settleAny` is the user taking the tab over: `noteUserMove()` sets it from every navigation entry point the user drives (`open()` when not from a navigation result, `navigateDirectory()`, `next()`, `previous()`, `nextFolder()`, `previousFolder()`), but only when no automatic move is running.
- Automatic moves never count as the user's: an `AutomaticMove` scope wraps `reconcileObservation()` (all of its `open`/`navigateDirectory` calls), `reconcileRenamedPath()`, and the tab code's own route (`goToTarget()`, used by `activateTab()`, `closeTab()` and Retry).

(b) *While the target folder is unavailable, the tab keeps its location and the view says so.*

- `goToTarget()` records the navigation sequence its folder route submits (`settleNavigation`). When that exact navigation fails (`switchFailed`), or the user cancels it (`cancelDirectoryNavigation()`, Esc in the path field; an offline share may hang rather than fail), `showUnavailable()` runs.
- `showUnavailable()` retires the previous tab's content: `++generation; cancel()` (the stale decode and the decode deadline go with it), clears `path`, the image, metadata and `lastDecodedPath`, clears the presentation image, empties `nav` and re-scopes the folder watch to nothing (the monitor ignores an empty scope). Nothing of the previous tab remains to reopen, so F5, watcher events, renames and deletes in that folder have nothing to act on.
- The pending position stays the target's stored location, so every checkpoint (and the destructor's clean close) records the right tab set.
- `tabs()` adds `unavailable: true` to the active row. Status and `directoryError`: "This tab's folder is unavailable: <folder>. Retry when it is back, or open another photo."
- **Retry:** F5 (`trigger("refresh")`) runs `retryTarget()` while the tab is unavailable: `settleOn` the stored location again and run the tab route. Success settles normally; failure returns to the unavailable state. The placeholder's Retry button calls `app.invoke("refresh")`, the same command, so the key and the button cannot drift apart.
- **Superseded route:** an automatic navigation that replaces the switch's pending folder route (following a rename, or the delete flow's step, in the moment before the route's result) drops that route's result. When the replacing result lands, the tab route runs again (`switchSuperseded` → `retryTarget()`), so the tab still ends settled or unavailable, never on the previous tab's content.
- `restoreWindow()` stays guarded while unavailable even though `path` is now empty: every route into the state sets `explicitPositionIntent` (the tab route's `open`/`navigateDirectory`). Tested with the claim retry below.

**Edits outside the tab regions** (declared):

- `open()`: `noteUserMove()` beside the existing `explicitPositionIntent` line (round 2's `++openSerial` stays).
- `next()`, `previous()`, `nextFolder()`, `previousFolder()`, `navigateDirectory()`: `noteUserMove()`.
- `reconcileObservation()`, `reconcileRenamedPath()`: one `AutomaticMove` scope line each.
- `cancelDirectoryNavigation()`: the switch-cancel branch.
- `trigger()`: `"refresh"` retries an unavailable tab.
- The navigation-result handler: `switchFailed` / `switchSuperseded`, the guarded no-images settle, `showUnavailable()` in place of the stale-path rescan, and the retry after a superseded route.

### The unavailable view (`ui/Main.qml`)

The ui-design-skills were read (`~/.agents/skills/ui-design-skills`: application-interface-design, layout-spacing, typography-ui, iconography-ui, component-geometry, responsive-layout, motion-ui) and the design-review checklist applied.

- It is a variant of the existing preview empty state (`emptyState`), not a new component. A new `.qml` file would need the root `qt_add_qml_module` list, which is outside the grant, and the empty state is already the canonical "nothing to show here" primitive.
- When the active tab row is `unavailable`:
  - the mascot, the brand heading and the tagline hide;
  - the existing `UiIcon` `folder` glyph shows, 48 px, `Theme.muted`, decorative (`Accessible.ignored`);
  - the title is "<folder name> is unavailable" in `Theme.textHeading`, medium weight, wrapping to two lines, `Accessible.role: AlertMessage`;
  - the full folder path, in native separators, sits below in `Theme.textSmall`/`Theme.muted`, middle-elided, with the full path as its accessible name;
  - one `primary` `UiButton` is labelled "Retry" (tooltip "Retry this tab's folder (F5)" from `app.shortcutHint("refresh")`);
  - the hint line says "Reconnect the drive or network share, then retry. Your other tabs keep their places."
- Theme tokens only (`Theme.gap*`, `Theme.text*`, `Theme.muted`, `Theme.text`); no new colours, radii or motion.
- Compact panes (under 280×320) keep the title and the Retry button, and hide the icon, path and hint, as the welcome variant does.
- Keyboard: the Retry button is in the Tab order (`UiButton`), and F5 is the shortcut. Focus is not taken automatically, because the canvas owns the window's shortcuts.
- **Limits:**
  - `emptyState` is hidden in grid and combined modes. There the grid shows an empty folder, and the status line carries the unavailable message; F5 still retries.
  - When closing the active tab leaves one tab whose folder is unavailable, `tabs()` is empty by contract (`window_slots` and earlier tests rely on that), so the welcome variant shows. The content is still retired, the status names the folder, and F5 retries. This is the same experience as a restore whose folder is unavailable. **For the lead:** a dedicated controller property would let the placeholder cover this case, but it needs a shared-header change (`include/glancelot/viewer_controller.hpp`); it was not made.
  - The new strings are not yet in `translations/glancelot_en_XA.ts`. `translation_coverage_ui` walks the default surfaces, where these elements are hidden, so it is unaffected; the catalog owner should regenerate it.
  - The placeholder was not rendered or screenshot in this round. `qmlcachegen` compiled it with the app target, and the UI suites that load `Main.qml` ran (below).

### Tests

New (`tests/tabs_recovery_tests.cpp`):

| Test | Covers |
| --- | --- |
| `reopenAfterAFailedFolderSwitchKeepsTheTarget` (rows: F5 refresh, external edit, external delete, F5 after stale decode) | The reviewer's four probe rows. The precondition asserts that the switch failed and live and stored both hold beta for tab 1. The edit row asserts that gamma's file really changed, and the delete row that it is gone. Each row waits 4 s for the watcher. Then it asserts live tabs, the unavailable state (active row flagged; no file, folder, image or loading), stored tabs, the active index and position, and the record `clean`. |
| `automaticMoveDuringAPendingSwitchNeverSettlesIt` (rows: rename followed, removal followed) | `reconcileRenamedPath` or `reconcileRemovedPaths` on the previous tab's photo while the switch's folder route is pending. The tab never settles on it, the route runs again, and the tab ends unavailable with every location kept. |
| `retryReopensTheTargetWhenItsFolderReturns` | beta's folder is renamed away (the share goes offline), the switch fails, the folder comes back, and F5/Retry reopens beta's own image. Live, stored, active index and `clean` are all asserted. |
| `retryWhileStillUnavailableKeepsEveryTabsLocation` | The window starts unclaimed. After the switch fails, `setWindowClaim` + `restoreWindow` run (the `app/main.cpp:211` retry); the restore must not replace the live set. Then F5/Retry twice with the folder still gone: still unavailable, every location kept, stored and `clean`. |

Changed on purpose (decision b): `staleDecodeCannotSettleADirectorySwitch` and the folder-route rows of `secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation` asserted that the stale photo was on screen after the failed route. They now assert the unavailable state instead. Their location assertions are unchanged.

### Fail before, pass after

Fail before: the new tests built on the merged tree (`f91a9db`) with the round 2 controller, Windows, offscreen. Temporary paths are shortened.

```
FAIL!  : TabsRecoveryTests::reopenAfterAFailedFolderSwitchKeepsTheTarget(F5 refresh) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 1)
FAIL!  : TabsRecoveryTests::reopenAfterAFailedFolderSwitchKeepsTheTarget(external edit) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 1)
FAIL!  : TabsRecoveryTests::reopenAfterAFailedFolderSwitchKeepsTheTarget(external delete) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, , gamma/c.png] active 1)
FAIL!  : TabsRecoveryTests::reopenAfterAFailedFolderSwitchKeepsTheTarget(F5 after stale decode) 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, alpha/a.png, alpha/a.png] active 1)
FAIL!  : TabsRecoveryTests::retryReopensTheTargetWhenItsFolderReturns() 'shows(viewer, files[1])' returned FALSE. (Untagged source assumed sRGB · display profile not yet managed showing gamma/c.png)
FAIL!  : TabsRecoveryTests::retryWhileStillUnavailableKeepsEveryTabsLocation() 'live.isEmpty()' returned FALSE. (live tab 1 holds the wrong location: live tabs [alpha/a.png, gamma/c.png, gamma/c.png] active 1)
Totals: 2 passed, 6 failed, 0 skipped, 0 blacklisted, 63138ms
```

The automatic-move test was written after the fix, so it was run against the round 2 controller as a mutation (M5 below).

Pass after (whole executable, final source):

```
Totals: 67 passed, 0 failed, 0 skipped, 0 blacklisted, 67735ms
```

67 = initTestCase, cleanupTestCase, 29 functions, and the data rows: 14 malformed, 14 legacy, 3 close-active, 2 stale-decode, 4 second-action, 4 reopen, 2 automatic-move. Each new location assertion reports the stored record or the live strip on failure.

### Mutation checks

Each check made one edit to `src/app/viewer_controller.cpp`, rebuilt `tabs_recovery_tests` only, ran the named tests, and reverted the source by script. The final build came after the revert.

| Mutation | Result |
| --- | --- |
| M5: the round 2 controller (`git show HEAD:…`, before this round) | `automaticMoveDuringAPendingSwitchNeverSettlesIt`: both rows fail (`[alpha, gamma/c2.png, gamma]` and `[alpha, "", gamma]`). |
| M6: the folder rule removed (`settlesIn()` always true) | `automaticMoveDuringAPendingSwitchNeverSettlesIt(removal followed)` fails: the delete flow's visit of gamma settled tab 1 (`[alpha, "", gamma]`). The other 9 selected rows pass, because the unavailable state alone removes what the reopen rows act on. So (a) is what protects the pending window. |
| M7: no rerun after a superseded route | Both automatic-move rows fail with "the active tab is not marked unavailable". The locations stay correct (the folder rule), but the previous tab's content would stay on screen. |
| M8: no unavailable state (`if(false)showUnavailable()`) | All four reopen rows and `retryWhileStillUnavailable…` fail with "the active tab is not marked unavailable". Every location assertion before that passes, so (a) alone keeps the data and (b) is what removes the previous tab's photo. |

```
M5: FAIL!  : …automaticMoveDuringAPendingSwitchNeverSettlesIt(rename followed) … live tabs [alpha/a.png, gamma/c2.png, gamma/c.png] active 1
    FAIL!  : …automaticMoveDuringAPendingSwitchNeverSettlesIt(removal followed) … live tabs [alpha/a.png, , gamma/c.png] active 1
    Totals: 2 passed, 2 failed
M6: FAIL!  : …automaticMoveDuringAPendingSwitchNeverSettlesIt(removal followed) … live tabs [alpha/a.png, , gamma/c.png] active 1
    Totals: 9 passed, 1 failed
M7: FAIL!  : …automaticMoveDuringAPendingSwitchNeverSettlesIt(rename followed) 'state.isEmpty()' … (the active tab is not marked unavailable)
    FAIL!  : …automaticMoveDuringAPendingSwitchNeverSettlesIt(removal followed) 'state.isEmpty()' … (the active tab is not marked unavailable)
    Totals: 2 passed, 2 failed
M8: FAIL!  : …reopenAfterAFailedFolderSwitchKeepsTheTarget(F5 refresh|external edit|external delete|F5 after stale decode) 'state.isEmpty()' … (the active tab is not marked unavailable)
    FAIL!  : …retryWhileStillUnavailableKeepsEveryTabsLocation() 'state.isEmpty()' … (the active tab is not marked unavailable)
    Totals: 2 passed, 5 failed
```

(The first M8 attempt did not compile: the script's replacement left a dangling `else`. It was corrected and rerun; the source was restored both times.)

### Build and targeted ctest

- **Build:** `build-slot.ps1 -Run { … scripts\build.ps1 -BuildDir build-claude-f015 }` on the final source, exit 0.
- **Targeted ctest:** `gate-lock.ps1 -Branch claude/f015-fix -Run { … }`, twice, with PATH prefixed by `.tools\python\Scripts` and the Qt `bin`. The regex is the brief's targeted suites, plus `folder_tree|viewer_acceptance|input_ui` and `translation_coverage_ui` (because `Main.qml` changed):

```
=== run 1
 1/11 Test  #7: viewer_acceptance ................   Passed   10.51 sec
 2/11 Test  #9: folder_tree ......................   Passed   10.05 sec
 3/11 Test #11: window_slots .....................   Passed   28.50 sec
 4/11 Test #13: input_ui .........................   Passed   80.29 sec
 5/11 Test #38: recovery .........................   Passed   13.94 sec
 6/11 Test #39: recovery_startup .................   Passed    0.48 sec
 7/11 Test #40: recovery_controller ..............   Passed   14.49 sec
 8/11 Test #41: recovery_process .................   Passed   28.47 sec
 9/11 Test #42: tabs_recovery ....................   Passed   56.33 sec
10/11 Test #45: translation_coverage_ui ..........   Passed   17.97 sec
11/11 Test #46: reference_pin ....................   Passed   17.87 sec
100% tests passed out of 11
Total Test time (real) = 279.45 sec
exit 0
=== run 2
 1/11 Test  #7: viewer_acceptance ................   Passed   11.96 sec
 2/11 Test  #9: folder_tree ......................   Passed   11.18 sec
 3/11 Test #11: window_slots .....................   Passed   30.16 sec
 4/11 Test #13: input_ui .........................   Passed   92.48 sec
 5/11 Test #38: recovery .........................   Passed   14.13 sec
 6/11 Test #39: recovery_startup .................   Passed    1.20 sec
 7/11 Test #40: recovery_controller ..............   Passed   17.68 sec
 8/11 Test #41: recovery_process .................   Passed   30.47 sec
 9/11 Test #42: tabs_recovery ....................   Passed   56.21 sec
10/11 Test #45: translation_coverage_ui ..........   Passed   17.30 sec
11/11 Test #46: reference_pin ....................   Passed   25.51 sec
100% tests passed out of 11
Total Test time (real) = 308.30 sec
exit 0
```

### Files changed in round 3

| File | Change |
| --- | --- |
| `CMakeLists.txt` | Merge conflict resolved (both blocks kept) |
| `cmake/TabsRecoveryTests.cmake`, `cmake/ReferencePinTests.cmake` | Viewer source lists and labels (grant) |
| `src/app/viewer_controller.cpp` | As above |
| `ui/Main.qml` | The `emptyState` unavailable variant (objectNames `emptyState`, `emptyStateAction`, `tabUnavailableTitle`, `tabUnavailableFolder`, `tabUnavailableIcon`) |
| `tests/tabs_recovery_tests.cpp` | Helpers `unavailableDiffers`; the tests above; two round 2 preconditions |
| `docs/handoff/STACK-F015-RECOVERY-HANDOFF.md` | This section |

No shared header or queue file changed.

### Pre-handoff checklist (AGENTS.md)

- Branch `claude/stack-f015-recovery`; tip SHA in the commit and the report; pushed fast-forward.
- Files changed: above. Lead-owned files: `CMakeLists.txt` (conflict only) and the two provisional cmake files, under the lead's one-time grant.
- Actual `ctest` output: above. The full offscreen suite was not run; the authoritative gate decides on the integrated commit.
- Failure and cancellation cases: folder unavailable on switch and close; F5, watcher edit, watcher delete, rename and delete-flow reconciles; a stale decode; a cancelled or superseded folder route; Retry success and repeated failure; the claim retry while unavailable.
- Unverified:
  - the placeholder's rendering (not screenshot);
  - Esc cancelling a hanging folder route (code path only; no hanging share here);
  - the transient B1 form (reasoned);
  - the grid/combined-mode and single-location presentation limits above;
  - physical, multi-window and GPU paths.
- UI: the ui-design-skills were read and applied; see "The unavailable view".
