# DELETE-01 verification

Original implementation: branch `agent/delete`, worktree `.worktrees/delete`. Continuation: branch `agent/delete-finish`, worktree `.worktrees/delete-finish`. Integrated in `agent/finish-integration`. State: done. Contract: docs/DELETE-PLAN.md. Requirements: FS-01, META-07, NAV-08, SYS-06, SYS-02.

## Summary

DELETE-01 adds asynchronous, safe deletion to the Recycle Bin / Trash for selected grid items and folder tree nodes, with pre-mutation revalidation, full confirmation dialogs, honest failure reporting, and comprehensive multi-window and controller reconciliation.

## Core interfaces and services

- `include/glancelot/file_operations.hpp`: Pure C++ planning (`planDeletion`), deduplication, root and read-only volume rejections, bounded recursive content counting, pre-mutation revalidation (`FileIdentity`, `snapshotTarget`), and execution over `TrashBackend` returning structured `DeleteResult`.
- `include/glancelot/file_operation_service.hpp`: Qt service coordinating two-phase planning and confirmation, asynchronous progress signals (`planReady`, `progress`, `finished`), cancellation, and injectable `TrashBackend`.
- `src/app/platform_trash.hpp`: Platform trash integration using `QFile::moveToTrash` with safe error mapping (`TrashUnavailable`, `InUse`, `Denied`, `Failed`).
- `src/app/file_operation_wiring.hpp`: Automatic reconciliation wiring between `FileOperationService`, `ViewerController`, and `FolderTreeModel`.
- `ui/DeleteFlow.qml`: Modal confirmation with keyboard safety (initial Cancel focus, ignoring repeated Enter), target and scope enumeration ("N files and M folders containing..."), progress overlay with cancellation support, and persistent error/rejection surfaces.
- `ui/UiButton.qml`: Added `"danger-quiet"` for hover/pressed caption close and retained filled `"danger"` for destructive confirmations.

## Verification evidence

### 1. Core and Service Tests (`file_operations_tests`, `file_operation_service_tests`)
All tests run with generated disposable fixtures in temporary directories:
- Deduplication of nested targets (dropping descendants).
- Rejection of root paths, special files, and read-only volumes.
- Symlinks / junctions: trashes link itself without touching target or traversing through directory symlinks.
- Bounded content counts without deep recursive traversal overhead.
- Pre-mutation identity and mtime/size revalidation (skipping Changed or Missing files).
- Cancellation before and mid-batch.
- Real trash on Windows / POSIX fallback; other mount handling.

### 2. UI and Reconciliation Tests (`grid_ui_tests`)
All 8 DELETE-01 test suites in `grid_ui_tests` pass:
- `deleteSelectionActionShowsOnlyWithSelection`
- `deleteConfirmationNamesTargetsAndScope`
- `deleteConfirmationSafetyAndEscape`
- `deleteProgressCancellationAndPartialResults`
- `deleteOpenedImageStepsToNextAndStaleListingCannotRestore`
- `deleteBrowsingFolderFromTreeVisitsNearestAncestor`
- `deleteRejectedOnlyPlanExplainsAndCannotConfirm`
- `deleteOtherWindowReconcilesThroughWatcher`

### 3. Integrated Test Suite
All 13 CTest targets pass cleanly on Windows (`scripts/build.ps1 -Test`):
- `navigation`: Passed
- `directory_listing`: Passed
- `file_operations`: Passed
- `viewer_acceptance`: Passed
- `thumbnails`: Passed
- `folder_tree`: Passed
- `pane_layout`: Passed
- `window_slots`: Passed
- `folder_grid`: Passed (66/66 test cases passed)
- `file_operation_service`: Passed
- `directory_model`: Passed
- `viewport`: Passed
- `themes`: Passed
Total time: 157.87s, 100% pass rate.
