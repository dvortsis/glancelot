# B3 review, round 3: merge of `main` into PR 9

**Verdict: APPROVED**

- Reviewed head: `70d864ecfdaa192d010b3d96885bca61818fec1b` (`claude/b3-export-ui`, PR 9).
- Parents: `e09805df83a6084477ddcfc62b27dbf48a256a76` (approved in `B3-REVIEW-R2.md`) and `53e4631c586c28138ddf63b07d345836dc4f205f` (`origin/main`). Merge base of those two: `f24f2e7344d0d4160a4b5a76b1e2bbb854361c46`.
- Scope: the lead's merge only, by a fresh reviewer that implemented none of this. B3 itself is covered by `B3-REVIEW.md` and `B3-REVIEW-R2.md`.
- **No build or test ran for this review.** This is a reading of the merge. The tester's full gate on this exact head follows, and it decides whether the merge works.

## Findings

Nothing blocking.

- **R3-N1 (note, non-blocking):** `translations/README.md` says the catalog has 654 messages; the merged catalog has 657, because PR 3 added 3. No test reads that number, and `main` was already stale the same way (it says 589 and has 592). Fix it at the next catalog regeneration.

## 1. The conflict resolution

`git show --remerge-diff 70d864e` lists one file, and its only change is the removal of the three conflict markers:

```
diff --git a/CMakeLists.txt b/CMakeLists.txt
remerge CONFLICT (content): Merge conflict in CMakeLists.txt
@@ -327,11 +327,8 @@ if(GLANCELOT_BUILD_VIEWER)
     include(cmake/CommandRoutingTests.cmake) # EN-06; needs the monitor-customization helper above
   endif()
   include(cmake/ReferencePinTests.cmake)
-<<<<<<< e09805d (B3: review delta, empty export keeps the target, locked file advice)
   include(cmake/ExportUiTests.cmake) # B3 (F070)
-=======
   include(cmake/StackConfirmTests.cmake)
->>>>>>> 53e4631 (Merge pull request #15 from dvortsis/lead/evidence-pr3)
   glancelot_attach_recovery_consumers()
```

The position is correct for both files:

- Every helper they call is defined earlier: `glancelot_test_labels` (`cmake/TestLabels.cmake`, included at `CMakeLists.txt:11`), `${ui_fonts}` (line 73) and `glancelot_attach_monitor_customization` (`cmake/MonitorCustomization.cmake`, included just above these lines).
- The order before `glancelot_attach_recovery_consumers()` does matter. That function walks the targets that exist when it runs and attaches recovery to any whose sources match `viewer_controller.cpp`. Both `export_ui_tests` and `stack_confirm_ui_tests` compile that file through `${GLANCELOT_VIEWER_SOURCES}`, and both are created before the call.
- The two files share no target, test, resource or shader name, and the order between them has no effect.
- `export_ui_tests` uses the same source list, include directories and link libraries as `stack_confirm_ui_tests`, the newest target from `main`, plus the export service. Nothing `main` added to the viewer sources is missing from it.

## 2. The auto-merges

**Unchanged B3 delta.** For each file both sides touched, the added and removed lines of B3's change against its base match those of the merged head against `main`, so git dropped and duplicated nothing:

```
ui/Main.qml                      B3-delta=13c24f21d88e  merged-delta=13c24f21d88e
src/app/command_registry.cpp     B3-delta=37727a7fdc15  merged-delta=37727a7fdc15
CMakeLists.txt                   B3-delta=4172da0aab8c  merged-delta=4172da0aab8c
translations/glancelot_en_XA.ts  B3-delta=3c28043a7fd3  merged-delta=3c28043a7fd3
translations/README.md           B3-delta=4c32971d88b3  merged-delta=4c32971d88b3
```

**`ui/Main.qml`.** No semantic collision with what `main` added (slideshow settling, tab strip, N-5):

- `exportFlow` is declared once, and `main` added no property or id of that name. `git diff e09805d 70d864e -- ui/Main.qml` contains only `main`'s slideshow and tab strip changes.
- The two sides work together. B3 adds the export dialog to `modalOpen`, and `onModalOpenChanged` calls `stopSlideshow()`, which now also stops `main`'s new `slideshowSkipTimer`. `slideshowStep()` also returns early while `modalOpen` is true. So opening the dialog ends a slideshow, as for any other modal.
- What the flow relies on survives: `photoGrid.deletionTargets()` (`main`'s `ui/PhotoGrid.qml` changes do not touch it), `fileGrid.directory`, `deleteFlow.showNotice`, `app.focusContent`, `fillText`, and `viewer.revealInFileManager`. `main`'s change to `viewer_controller.hpp` only adds `navigating`.
- `main` changed no other QML file that `ui/ExportDialog.qml` depends on. On its side, under `ui/`, only `Main.qml` and `PhotoGrid.qml` changed.
- `tests/export_ui_tests.cpp` does not reference the slideshow or tab strip objects `main` changed.

**`src/app/command_registry.cpp`.** `git diff e09805d 70d864e -- src/app/command_registry.cpp` is empty: `main` did not change the file.

- `export-selection` is registered once. The only repeated ids, `delete-selection`, `delete-current-file` and `delete-current-folder`, are repeated on `main` too; they are the Recycle Bin and Trash wordings of the same command.
- `Ctrl+Shift+E` belongs only to `export-selection`, in the registry and in QML. The shortcut `main` added is `Ctrl+Shift+F`, handled in `ui/PhotoGrid.qml`, which is a different key.

**`translations/glancelot_en_XA.ts`.** It is one valid catalog with nothing lost or duplicated:

```
merged total 657 unfinished 0
origin/main total 592
e09805d total 654
ui/ExportDialog.qml literal qsTr 65 catalogued 65 missing []
ui/Main.qml literal qsTr 139 catalogued 139 missing []
ui/PhotoGrid.qml literal qsTr 24 catalogued 24 missing []
```

- 592 on `main` plus B3's 65 `ExportDialog` messages gives 657. B3's delta against `main` is that single new context, with no line removed.
- The parse found no duplicate context, and no duplicate source and comment pair in any context.
- PR 3's 3 new messages and its replaced grid description arrived from `main` intact. B3 wrote its own catalog without them, and the `ExportDialog` context in the merge matches B3's.

## 3. The merged diff against `main`

`git diff origin/main 70d864e --stat` has 32 files, 3,749 insertions and 2 deletions. The list is identical to B3's own change against its base:

```
b3 own: 32, merged: 32
file lists identical
```

Those files are `CMakeLists.txt`, `cmake/ExportUiTests.cmake`, `docs/handoff/B3-HANDOFF.md`, `docs/verification/B3/*`, `include/glancelot/selection_export.hpp`, `src/app/command_registry.cpp`, `src/app/selection_export_service.{cpp,hpp}`, `src/core/selection_export.cpp`, `tests/export_ui_tests.cpp`, `translations/README.md`, `translations/glancelot_en_XA.ts`, `ui/ExportDialog.qml` and `ui/Main.qml`.

## Not verified here

- No configure, build or test ran. The CMake ordering above comes from reading the files, so the tester's full gate is what proves it.
- The `viewer` and `core` checks on this head had not finished when this review was written: `guard` had passed, and the others had no conclusion yet.
