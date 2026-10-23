# STACK-F015-RECOVERY handoff

Track session S5 (stack viewer), Phase 0 cycle 1. Contract: LEAD-WRAPUP-2026-10-22.md (F015 bullet), LEAD-TAKEOVER-2026-10-22.md section 4 and LEAD-PLAN-2026-10-22.md section 5. Not self-approved; a fresh reviewer decides.

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
  - About lines 1486 and 1502: `activateTab()`, and `closeTab()` of the active tab, still mark the position dirty but stop the save timer. Their own navigation notes the settled position and restarts it, so a slow decode cannot persist a half-switched tab set. `newTab()` and closing an inactive tab save as before. The final close (`writePosition(true)`) accepts the newest snapshot before `close(…, allowClean)`, so the session never cleans before it persists.
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

### Exact shared-header diff

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
- A tab switch whose target fails to decode leaves the position dirty without a timer. The newest snapshot is saved at the next successful navigation or at close. If the process dies first, recovery offers the previous coherent checkpoint, and the record stays `dirty`, never falsely clean.
- `docs/WORK-QUEUE.yaml` was not touched (lead-owned).
- No UI files changed in this packet.
