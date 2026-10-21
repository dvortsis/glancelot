# Task Packet: RENAME-01 (F018 In-Browser File & Folder Rename and New Folder)

**Milestone**: M3 / M4  
**Requirements**: `SYS-06`, `F018`  
**Owner Lane**: Gemini (`agent/*`)  
**Branch**: `agent/rename`  
**Worktree**: `.worktrees/rename`  
**Build Directory**: `build-rename`  
**Memory Action**: `act_mvg8jdxp_be1746b241cb`  

---

## 1. Objective & Scope

Task `RENAME-01` introduces safe, atomic, in-browser single-item renaming and new folder creation for Glancelot:
1. **Single-Item Rename (`F2`)**:
   - Rename any selected file or folder in grid, folder tree, or currently viewed photo in preview.
   - Preserves source file safety (`SYS-06`): explicit target snapshot, validation before execution, honest error reporting.
   - Comprehensive filename validation:
     - Rejects empty, whitespace-only, and excessively long names (> 255 chars).
     - Rejects illegal filesystem characters (`\ / : * ? " < > |`) and ASCII control characters.
     - Rejects Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`..`COM9`, `LPT1`..`LPT9`), including with extensions.
     - Rejects trailing spaces and periods on Windows.
   - Robust case-only rename handling:
     - Detects and cleanly executes case-only renames (e.g. `image.jpg` -> `Image.jpg`) on case-preserving/case-insensitive filesystems without false collision rejection.
   - Collision protection:
     - Rejects renames that collide with existing files or directories.
2. **New Folder Creation (`Ctrl+Shift+N`)**:
   - Creates a new directory in the active browsing folder (or selected folder in tree).
   - Validates folder name using the same strict rules.
   - Provides intelligent folder name suggestions (e.g. `New Folder`, `New Folder (2)`).
3. **Workspace State Reconciliation**:
   - Atomically updates `ViewerController`'s opened file path (`filePath`) and name (`fileName`) if the currently displayed image is renamed.
   - Atomically updates current browsing folder (`directory`) if the active folder or an ancestor directory is renamed.
   - Reconciles `FolderHistory`: updates matching history entries so back/forward navigation remains consistent and points to valid paths.
   - Emits `filesInvalidated()` to reload grid and filmstrip models with preserved selection on the renamed/created item.
4. **Ergonomic Native UI Dialog (`ui/RenameFlow.qml`)**:
   - Built with canonical native `UiDialog`, `UiTextField`, `UiButton`, and `Theme.qml` tokens.
   - Pre-selects base name without file extension for fast editing.
   - Live validation error feedback in real time as the user types.
   - Fully keyboard accessible: `Enter` to confirm, `Escape` to cancel.

---

## 2. Shared Interfaces & Contracts

### `include/glancelot/mutation_service.hpp`
- `glancelot::MutationService : public QObject`
- Validation:
  - `static bool isValidFileName(const QString& name, QString* error = nullptr);`
  - `Q_INVOKABLE QVariantMap validateTargetName(const QString& parentDirectory, const QString& newName, const QString& currentPath = QString()) const;`
- Execution:
  - `Q_INVOKABLE QVariantMap renamePath(const QString& sourcePath, const QString& newName);`
  - `Q_INVOKABLE QVariantMap createFolder(const QString& parentDirectory, const QString& folderName);`
  - `Q_INVOKABLE QString suggestNewFolderName(const QString& parentDirectory, const QString& baseName = "New Folder") const;`
- Signals:
  - `void pathRenamed(const QString& oldPath, const QString& newPath);`
  - `void folderCreated(const QString& folderPath);`

### `include/glancelot/folder_history.hpp`
- `void reconcileRenamedPath(const QString& oldPath, const QString& newPath);`

### `include/glancelot/viewer_controller.hpp`
- Property:
  - `Q_PROPERTY(glancelot::MutationService* mutationService READ mutationService CONSTANT)`
- Commands:
  - `{"rename", "Rename current file or folder", "F2"}`
  - `{"new-folder", "Create new folder", "Ctrl+Shift+N"}`
- Slots / Reconciliation:
  - `Q_INVOKABLE void reconcileRenamedPath(const QString& oldPath, const QString& newPath);`
  - `Q_INVOKABLE void reconcileCreatedFolder(const QString& folderPath);`

---

## 3. Verification & Acceptance Criteria

- `tests/mutation_service_tests.cpp` executes with:
  1. Filename validation: empty, illegal characters (`/ \ : * ? " < > |`), Windows reserved device names, trailing periods/spaces, max length.
  2. Collision detection and case-only rename distinction.
  3. Single file rename and case-only rename on temporary filesystem.
  4. Directory rename and case-only directory rename.
  5. New folder creation and automatic duplicate name suggestions (`New Folder`, `New Folder (2)`).
  6. Atomic `ViewerController` and `FolderHistory` path reconciliation.
  7. Permission denied / non-existent source error handling.
- Full Windows native suite (`scripts/build.ps1 -Test`) and Linux Docker build pass.
