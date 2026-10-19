# FOLDER-HISTORY-01 Implementation Plan

Task: `FOLDER-HISTORY-01`
Requirements: `NAV-01`, `NAV-08`, `UX-13`, `IN-06`
Feature: `F001`
Owner: Gemini lane (`agent/folder-history`)
Worktree: `.worktrees/folder-history`
Build: `build-folder-history`

---

## 1. Executive Summary & Goals

`FOLDER-HISTORY-01` implements feature `F001` from `docs/FEATURE-OPPORTUNITIES.md`:
"Back/forward folder history with preserved scroll, selection and preview; makes exploration reversible."

The feature provides bidirectional browser-style folder navigation history:
- Visiting a new folder pushes the previous folder onto the back stack and resets the forward stack.
- Stepping through photos inside the same folder (via arrows, filmstrip, or mouse wheel) updates the remembered image and selection of the active folder entry without pushing redundant history entries.
- Navigating back (`Alt+Left`, mouse back button, or UI back button) restores the previous folder, along with its last viewed photo and selection.
- Navigating forward (`Alt+Right`, mouse forward button, or UI forward button) returns forward through the branch.
- Missing or deleted folder locations are handled gracefully: if a target location is missing, it is reported or skipped without corrupting the current view or the history stack.
- The history stack is bounded (default 50 entries) to prevent unbounded memory retention.

---

## 2. Shared Contracts & Architecture

### 2.1 Shared Header: `include/glancelot/folder_history.hpp`

The contract defines `FolderHistoryEntry` and `FolderHistory`:
- `FolderHistoryEntry`:
  - `QString directory`: canonical normalized path.
  - `QString filePath`: last active image preview in the folder.
  - `QString selectedPath`: last selected item in the folder grid.
  - `int scrollPosition`: last scroll index or offset.
- `FolderHistory`:
  - `canGoBack`, `canGoForward`: observable boolean properties for UI enablement.
  - `backDirectory`, `forwardDirectory`: observable target paths for informative tooltips ("Back to Pictures").
  - `currentDirectory`: current active folder.
  - `recordVisit(directory, filePath, selectedPath, scrollPosition)`: records folder navigation.
  - `updateCurrent(filePath, selectedPath, scrollPosition)`: updates active entry without pushing.
  - `goBack()`, `goForward()`: pops and updates stacks, returning the destination entry.
  - `navigateBack()`, `navigateForward()`: QML-invokable convenience actions emitting `navigationRequested`.

### 2.2 Integration with `ViewerController` and UI

- `ViewerController` or `app/main.cpp`:
  - Registers `FolderHistory` and synchronizes with `ViewerController::directory` and `ViewerController::filePath`.
  - Connects `navigationRequested` to `viewer.navigateDirectory` or `viewer.open`.
  - Exposes `folderHistory` to the QML context.
  - Adds commands to the registry:
    - `{"navigate-back", "Go back to previous folder", "Alt+Left"}`
    - `{"navigate-forward", "Go forward to next folder", "Alt+Right"}`

### 2.3 UI & Key Bindings

- **Left HUD (`FolderTree.qml`) Address Row**:
  - Adds Back (`chevron-left`) and Forward (`chevron-right`) buttons directly before `folderAddress`:
    `[ Back ] [ Forward ] [ Address TextField ] [ Copy ]`
  - Enabled states bound to `folderHistory.canGoBack` and `folderHistory.canGoForward`.
  - Tooltips display destination folder: `"Back to " + folderHistory.backDirectory` / `"Forward to " + folderHistory.forwardDirectory`.
- **Keyboard & Mouse Navigation**:
  - `Alt+Left` / `Qt.Key_Back` -> `navigateBack()`.
  - `Alt+Right` / `Qt.Key_Forward` -> `navigateForward()`.
  - Mouse `Qt.BackButton` (XButton1) and `Qt.ForwardButton` (XButton2) handled in `Main.qml`.

---

## 3. Verification Plan

A dedicated test suite `tests/folder_history_tests.cpp` (target `folder_history_tests`) verifies:
1. Initial state: empty back/forward stacks; `canGoBack` and `canGoForward` are false.
2. Distinct folder visit pushes previous folder to back stack, clears forward stack.
3. Intra-folder image steps update current entry's file/selection without pushing new stack entries.
4. `goBack()` pops from back stack, pushes current to forward stack, restores directory and remembered image.
5. `goForward()` pops from forward stack, pushes current to back stack, restores directory and remembered image.
6. Linear branch reset: navigating to a new folder after going back clears forward history.
7. Capacity bounds: pushing past 50 entries drops oldest entries without data corruption.
8. Duplicate visit suppression: navigating to the currently active folder does not duplicate entries.
9. Missing mount/folder recovery: attempting to navigate to a deleted folder fails gracefully without crashing or corrupting history.
10. Windows case-folding and path normalization.
11. Windows CTest suite and Linux container build.
