# DELETE-01 contract and delivery

The lead froze this contract on October 18, 2026. Requirements: FS-01, META-07 (the explicit-deletion subset), NAV-08, SYS-06 and SYS-02.

Implementation runs on branch `agent/delete` from `main` once FILE-GRID-01 is integrated, in worktree `.worktrees/delete` with build directory `build-delete`. Use native Qt/QML and the existing Theme tokens and UI primitives (`UiButton`, `UiIcon`, dialogs consistent with the existing settings and confirmation surfaces). Apply the relevant ui-design-skills to the confirmation and result UI, and run an independent native design-review.

## Frozen interfaces

- `include/glancelot/file_operations.hpp` is the core contract. It covers:
  - snapshot and revalidation: `FileIdentity`, `MutationTarget` and `snapshotTarget`;
  - planning: `planDeletion`, with deduplication, root and read-only rejection, and bounded content counts;
  - execution: `executeDeletion` over a `TrashBackend`, returning a per-target `DeleteResult`.
  Its tests are pure C++ and live in `glancelot_core`.
- `include/glancelot/file_operation_service.hpp` is the Qt service. It runs the two-step `plan` and `confirm` flow, plus `cancel`, `busy` and the signals, and has an injectable backend for tests.
- One controller addition is pre-approved: `Q_INVOKABLE void reconcileRemovedPaths(const QStringList& paths)` on `ViewerController` (see Reconciliation). No other shared-header change is allowed without the lead.
- CMake: add `src/core/file_operations.cpp` to `glancelot_core`, a `file_operations` core test target, and a `file_operation_service` Qt test target. The service is compiled into `glancelot` and `grid_ui_tests`. The lead reviews the root CMake diff at integration.

## Behaviour

- **Entry points in this task:**
  - The grid header shows a "Move to Recycle Bin" / "Move to Trash" action when the selection is not empty. Its targets are the selected paths, or the cursor item when nothing is selected.
  - A tree row's context menu (right-click, the Menu key or Shift+F10) offers the same action for that row's folder.
  - The command `delete-selection` appears in command search with no default binding. DELETE-KEYS-01 adds the bindings and the current-file and current-folder commands.
- **Planning:**
  - Planning runs on a worker and is cancellable. Targets are read without following links.
  - A target inside another target is dropped as Duplicate, and the confirmation mentions it.
  - These targets are rejected with a reason, and a request containing them still offers the rest:
    - filesystem roots (drive roots, `\\server\share`, `/`);
    - missing paths;
    - special files;
    - read-only volumes (`FILE_READ_ONLY_VOLUME` / `ST_RDONLY`).
  - Folder content counts are bounded (`contentLimit`) and never follow directory links. When the walk is incomplete, the confirmation says "at least".
- **Paired files:** only the named items are affected. Sidecars (`.xmp`), RAW+JPEG pairs and other same-stem files are never added implicitly, and the confirmation says nothing else is removed. Paired-file sets stay M4 (META-07).
- **Confirmation (modal, keyboard-complete):**
  - It names the targets: up to 5 names, then "and N more".
  - It states the scope, for example "3 files and 2 folders (containing 140 files and 9 folders)".
  - It names the destination ("Recycle Bin" or "Trash") and lists any rejected items with their reasons.
  - Initial focus is on Cancel, so a held or repeated Enter or Delete cannot confirm. Escape cancels.
  - The destructive action uses the danger variant, and its label says what happens ("Move to Recycle Bin").
  - The captured plan is immutable while the dialog is open. A selection or navigation change behind the dialog does not change it.
- **Execution:**
  - At most one execution runs at a time. Each target is revalidated immediately before its move: identity, plus size and modified time for files.
  - A Changed or Missing target is skipped and reported.
  - Cancellation stops before the next item.
  - Progress shows for multi-item batches, with a Cancel button.
  - The platform backend calls `QFile::moveToTrash` and maps errors to `DeleteOutcome`. It must never permanently delete. On Windows, verify that Qt aborts when the item would be deleted instead of recycled (`TSF_DELETE_RECYCLE_IF_POSSIBLE` in `PreDeleteItem`). If that is not guaranteed, implement the backend directly with `IFileOperation` and that sink. When the trash is unavailable, the item is reported as `TrashUnavailable`, with no permanent-delete offer in this task.
- **Results:**
  - On full success, a short notice: "Moved 4 items to the Recycle Bin".
  - On partial or total failure, a persistent results surface lists each failed path with its plain-language reason.
  - Recovery is honest: the notice says items can be restored from the Recycle Bin/Trash. There is no in-app undo in this task.
- **Reconciliation:** after `pathsRemoved`, the following happen:
  - The grid refreshes, keeping the selection of survivors.
  - The tree refreshes the affected parents.
  - `reconcileRemovedPaths` handles the viewer:
    - If the opened image was removed, it moves to the next eligible image by the deleted-current anchor rule.
    - If the browsing folder or one of its ancestors was removed, it navigates to the nearest existing ancestor.
    - It persists the new session location.
  - Thumbnails for removed paths are invalidated.
  - Stale listing results (older generations) cannot restore removed entries.
  - Other windows (processes) reconcile through their existing filesystem invalidation.
- **Read-only mode:** when every target is on a read-only volume, the action is disabled with a tooltip or explanation, not hidden.

## Boundaries

This task covers deletion only, and only when the user starts it. It adds no copy, move, rename or permanent delete; tagged batch operations and culling stay M4. Viewing stays read-only, and no code path reachable from browsing or decoding mutates the filesystem.

## Verification

Use generated, disposable fixtures under a temporary directory only. Never touch user media or the real Recycle Bin outside test files.

Core tests (`file_operations`):

- deduplication of an ancestor and its descendant;
- root rejection;
- a missing path;
- a read-only volume, where creatable (otherwise reported unverified);
- a link target that is the link alone, where its target survives;
- a directory link inside a target folder that is not counted or followed;
- a bounded content count with `contentCountComplete=false`;
- revalidation (a file replaced by a same-name file becomes Changed; a touched file becomes Changed; a removed file becomes Missing);
- cancellation before and during a batch;
- a backend returning TrashUnavailable, InUse or Denied, which leaves the file present;
- partial success ordering.

Service tests:

- a superseded plan cannot be confirmed;
- `confirm` after `cancel` returns false;
- busy rejection;
- signals and result maps;
- the injected backend.

A real-trash smoke test on generated files is allowed. Clean up any test items it leaves in the Recycle Bin if that is feasible; otherwise record them.

UI tests (`grid_ui_tests`):

- the action shows only with a selection;
- the confirmation names targets and scope;
- initial focus is on Cancel, and Enter repeat or Escape does not delete;
- a captured plan survives a selection change;
- results show partial failure;
- the grid, tree and preview reconcile, including the opened image being deleted and the browsing folder being deleted from the tree;
- a stale listing cannot restore an entry;
- dark/light and compact/wide captures behind `GLANCELOT_REVIEW_CAPTURE`.

Also check two windows as two processes, where possible. Run the full suite with `scripts/build.ps1 -BuildDir build-delete -Test`, and run Linux through the Docker image. Linux trash on another mount is unverified unless it is exercised.
