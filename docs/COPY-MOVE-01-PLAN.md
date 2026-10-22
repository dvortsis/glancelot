# Task Packet: COPY-MOVE-01 (F020 Asynchronous File Copy & Move Queue)

**Milestone**: M3 / M4  
**Requirements**: `META-07`, `SYS-06`, `F020`  
**Owner Lane**: Gemini (`agent/*`)  
**Branch**: `agent/copy-move`  
**Worktree**: `.worktrees/copy-move`  
**Build Directory**: `build-copy-move`  
**Memory Action**: `act_mvg9x5co_b84238ed618a`  

---

## 1. Objective & Scope

Task `COPY-MOVE-01` introduces a non-blocking, safe asynchronous file copy and move queue for Glancelot:
1. **Asynchronous Background Transfer Queue (`CopyMoveService`)**:
   - Non-blocking execution on dedicated worker thread/thread pool off the UI and render thread.
   - Supports `Copy` and `Move` modes for individual files and whole directory trees.
   - Chunked transfer (64 KiB chunks) with responsive `pause`, `resume`, and `cancel` lifecycle controls.
   - Real-time progress reporting: transferred bytes, total bytes, completed items, total items, and active item filename.
2. **Strict Requirement Verification (`META-07` & `SYS-06`)**:
   - **Cross-Volume Move Verification**:
     - Accurately detects whether source and destination reside on the same or different filesystem volumes.
     - Same volume: executes atomic filesystem rename.
     - Cross-volume: executes chunked copy, verifies destination file size and integrity, and ONLY deletes the source file after destination verification succeeds. If copy is interrupted or fails, destination residue is cleaned and original source remains untouched.
   - **Collision Policies**:
     - `RenameBoth` (default: creates `file (2).jpg` automatically).
     - `Overwrite` (replaces destination).
     - `Skip` (leaves destination intact and reports item skipped).
   - **Link Protection**:
     - Symlinks, junctions, and directory shortcuts are never followed recursively to prevent circular traversal.
3. **Clipboard Cut / Copy / Paste Integration**:
   - `Ctrl+X` (Cut / Move selection).
   - `Ctrl+C` (Copy selection).
   - `Ctrl+V` (Paste to current browsing directory).
4. **Workspace State Reconciliation**:
   - If the currently opened photo in `ViewerController` is moved, atomically updates `viewer.filePath` to the destination path.
   - Emits `filesInvalidated()` so folder grids, filmstrips, and folder trees refresh without stale file entries.
5. **Ergonomic Native QML UI (`ui/TransferHud.qml`)**:
   - Floating native HUD banner displaying progress bar, transferred MB / total MB, item counter, pause/resume button, and cancel button.
   - Built with native `Theme.qml` tokens.

---

## 2. Shared Interfaces & Contracts

### `include/glancelot/copy_move_service.hpp`
- `glancelot::CopyMoveService : public QObject`
- Enums:
  - `TransferMode { Copy, Move }`
  - `CollisionPolicy { Skip, Overwrite, RenameBoth }`
- Properties:
  - `bool active READ active NOTIFY activeChanged`
  - `bool paused READ paused NOTIFY pausedChanged`
  - `int totalItems READ totalItems NOTIFY progressChanged`
  - `int completedItems READ completedItems NOTIFY progressChanged`
  - `qint64 totalBytes READ totalBytes NOTIFY progressChanged`
  - `qint64 transferredBytes READ transferredBytes NOTIFY progressChanged`
  - `qreal progress READ progress NOTIFY progressChanged`
  - `QString currentItem READ currentItem NOTIFY progressChanged`
  - `QString statusMessage READ statusMessage NOTIFY progressChanged`
- Invokable Methods:
  - `quint64 enqueueTransfer(QStringList sourcePaths, QString destDirectory, TransferMode mode, CollisionPolicy collision = RenameBoth)`
  - `void pause()`, `void resume()`, `void cancel()`
  - `void markCut(QStringList paths)`, `void markCopy(QStringList paths)`, `void pasteTo(QString destDirectory)`, `void clearClipboard()`
  - `bool hasClipboardTargets() const`, `QString clipboardMode() const`, `QStringList clipboardTargets() const`
- Signals:
  - `void activeChanged()`, `void pausedChanged()`, `void progressChanged()`
  - `void itemFinished(QString source, QString dest, bool success, QString error)`
  - `void batchFinished(quint64 batchId, int successfulCount, int failedCount)`
  - `void pathsMutated(QStringList sources, QStringList destinations, bool isMove)`

### `include/glancelot/viewer_controller.hpp`
- Property:
  - `Q_PROPERTY(glancelot::CopyMoveService* copyMoveService READ copyMoveService CONSTANT)`
- Commands:
  - `{"cut-selection", "Cut selected files", "Ctrl+X"}`
  - `{"copy-selection", "Copy selected files", "Ctrl+C"}`
  - `{"paste", "Paste files to current folder", "Ctrl+V"}`

---

## 3. Verification & Acceptance Criteria

- `tests/copy_move_tests.cpp` executes with:
  1. Single file copy and move on same volume.
  2. Cross-volume move: verified copy before source removal (`META-07`).
  3. Directory tree recursive copy and move.
  4. Collision handling: `RenameBoth` (e.g. `file (2).jpg`), `Overwrite`, `Skip`.
  5. Pause and resume operations during active byte transfer.
  6. Cancellation during active transfer (preserves source, cleans destination).
  7. Clipboard Cut / Copy / Paste workflow (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`).
  8. Atomic `ViewerController` reconciliation on moved active files.
- Full Windows native suite (`scripts/build.ps1 -Test`) and Linux Docker build pass.
