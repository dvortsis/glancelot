# PLATFORM-OPS-01 Independent Verification Report

- **Task**: PLATFORM-OPS-01 (F065) — Reveal in system file manager and open terminal at current location safely
- **Commit Reviewed**: `cf1a97f` (`PLATFORM-OPS-01 (F065): Address review findings - directory reveal, fallback args, user feedback, and test expansions`)
- **Initial Commit Reviewed**: `1c2a5ce`
- **Review Workspace**: `C:\src\Github\Glancelot\.worktrees\platform-ops-review`
- **Review Date**: 2026-10-18
- **Reviewer**: Independent Reviewer Agent (clean review worktree)
- **Status / Recommendation**: **APPROVE**

---

## 1. Executive Summary

Commit `cf1a97f` provides the complete remediation for task `PLATFORM-OPS-01` (Feature `F065`) addressing all findings from the initial review of commit `1c2a5ce`.

The feature and its remediation encompass:
1. `glancelot::PlatformIntegration` static helper class with `revealInFileManager` and `openTerminal` routines:
   - On Windows: Directories are opened directly via `explorer.exe <native>`. Files are highlighted within their parent folder via Shell API `SHOpenFolderAndSelectItems` using parent folder PIDL and child item ID, backed by safe `/select,` fallback. COM is properly initialized with `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`.
   - On Linux: Directories and files are cleanly opened via `xdg-open <targetDir>`. Terminal launches safely probe `$TERMINAL` and standard desktop emulators.
2. `ViewerController` invokables `revealInFileManager` and `openTerminal` with sensible target fallbacks:
   - User feedback is provided on all failure paths via `d->status` and `stateChanged()`.
   - When no file or directory is open, an actionable status is set ("No photo or folder is open to reveal." / "No folder is open to open a terminal.").
   - When launching fails, an error status is set ("Could not open file manager." / "Could not open terminal.").
3. Discoverable commands registered in `ViewerController`:
   - `reveal-in-file-manager`: bound to `Ctrl+Alt+R`
   - `open-terminal`: bound to `Ctrl+Alt+T`
4. Main QML integration in `ui/Main.qml`:
   - Supported in grid scope via `gridAllowsCommand`.
   - Shortcuts suppressed when `modalOpen` or `editingText` are active.
5. Comprehensive test coverage:
   - `tests/platform_integration_tests.cpp` verifies directory reveal, file reveal, root directory (`QDir::rootPath()`), Unicode and spaces.
   - `tests/grid_ui_tests.cpp` verifies command registration, default bindings, and feedback reporting when target paths are empty or invalid.

All 14 CTest test suites built and passed cleanly (100% pass in 270.37s).

The independent review recommendation is **APPROVE**.

---

## 2. Requirement and Acceptance Criteria Matrix

| Criteria / Requirement | Description | Evaluation / Evidence | Status |
| :--- | :--- | :--- | :--- |
| **1. No Shell Interpolation** | Argument arrays without shell string interpolation; no shell injection vulnerabilities. | `QProcess::startDetached` is called with argument lists (`QStringList`). For `wt.exe`, arguments are `{"-d", nativeDir}`. For `powershell.exe`, single-quote escaping (`replace("'", "''")`) and `-LiteralPath` prevent command injection. Linux invokes `$TERMINAL` / candidates with `targetDir` as working directory parameter. | **Pass** |
| **2. Path Handling** | Proper handling of non-existent paths, root/drive paths, Unicode paths, and spaces. | Empty and nonexistent paths safely return `false`. Unicode and spaces verified by `pathsWithSpacesAndUnicodeValidation`. Root directory terminal launch verified by `openTerminalRootDirectoryReturnsTrue`. | **Pass** |
| **3. Directory vs File Reveal** | Highlight file or open folder; directories opened directly; files highlighted in parent. | In `cf1a97f`, `PlatformIntegration::revealInFileManager` branches on `fi.isDir()`. Directories launch `explorer.exe <native>`. Files clone the PIDL, extract parent/child, and invoke `SHOpenFolderAndSelectItems(pidlFolder, 1, &pidlChild, 0)`. Verified by `revealDirectoryVsFileBehaviors`. | **Pass** |
| **4. Safe Fallback** | Safe fallback: Windows Terminal (`wt.exe`) with PowerShell fallback; `SHOpenFolderAndSelectItems` with `explorer.exe /select,"path"` fallback. | Windows Terminal probed via `QStandardPaths::findExecutable("wt.exe")`; falls back to `powershell.exe`. `SHOpenFolderAndSelectItems` attempted via Shell API; falls back to `{"/select,", native}` preventing argument quoting bugs. | **Pass** |
| **5. ViewerController Invokables** | `revealInFileManager`, `openTerminal` with sensible target fallbacks. | Exposed as `Q_INVOKABLE bool` with default `QString()` args. Fallbacks resolve: explicit path/URL -> current preview file -> current browsing directory -> parent directory. | **Pass** |
| **6. User Feedback** | Graceful fallback and user feedback if unavailable. | In `cf1a97f`, `ViewerController` sets `d->status` and emits `stateChanged()` on empty targets or launch failures. Verified by `grid_ui_tests.cpp`. | **Pass** |
| **7. Command Registration** | Commands registered: `reveal-in-file-manager` (`Ctrl+Alt+R`), `open-terminal` (`Ctrl+Alt+T`). | Registered in `src/app/viewer_controller.cpp` (`registry`). Verified in `tests/grid_ui_tests.cpp` (`platformOperationsShortcutsRegisteredAndBound`). | **Pass** |
| **8. Grid Scope Allowed** | Allowed in grid scope via `gridAllowsCommand`. | Both `reveal-in-file-manager` and `open-terminal` added to `gridAllowsCommand` in `ui/Main.qml`. | **Pass** |
| **9. Shortcut Precedence** | Text fields and modal dialogs retain shortcut precedence. | Shortcuts instantiated via `Instantiator` in `ui/Main.qml` with `enabled: !app.modalOpen && !app.editingText`. | **Pass** |
| **10. Native UI Quality & Accessibility** | Design tokens, accessibility, error feedback. | Clean QML structure, accessible status integration via `statusToast` and status bar. | **Pass** |

---

## 3. Remediation Review (Commit `cf1a97f`)

### Finding 1: Directory vs File Reveal Behavior on Windows
- **Resolution**:
  In `src/app/platform_integration.cpp`:
  ```cpp
  #ifdef Q_OS_WIN
      const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
      if (fi.isDir()) {
          return QProcess::startDetached("explorer.exe", {native});
      }
      const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
      const bool comInitialized = (hrCo == S_OK || hrCo == S_FALSE);
      PIDLIST_ABSOLUTE pidlFile = ILCreateFromPathW(reinterpret_cast<LPCWSTR>(native.utf16()));
      if (pidlFile) {
          PIDLIST_ABSOLUTE pidlFolder = ILClone(pidlFile);
          ILRemoveLastID(pidlFolder);
          PCUITEMID_CHILD pidlChild = ILFindLastID(pidlFile);
          HRESULT hr = SHOpenFolderAndSelectItems(pidlFolder, 1, &pidlChild, 0);
          ILFree(pidlFolder);
          ILFree(pidlFile);
          if (comInitialized) CoUninitialize();
          if (SUCCEEDED(hr)) return true;
      } else if (comInitialized) {
          CoUninitialize();
      }
  ```
  - Directories are now launched directly with `explorer.exe <native>`, opening the folder itself.
  - Files are now properly highlighted in their parent directory using `SHOpenFolderAndSelectItems` with a 2-level PIDL breakdown (`pidlFolder` and `pidlChild`).
  - COM is initialized and uninitialized safely.
- **Status**: **RESOLVED**

### Finding 2: Missing User Feedback When Unavailable or Failed
- **Resolution**:
  In `src/app/viewer_controller.cpp`:
  ```cpp
  bool ViewerController::revealInFileManager(const QString& path) {
      ...
      if (target.isEmpty()) {
          d->status = "No photo or folder is open to reveal.";
          emit stateChanged();
          return false;
      }
      const bool ok = glancelot::PlatformIntegration::revealInFileManager(target);
      if (!ok) {
          d->status = "Could not open file manager.";
          emit stateChanged();
          return false;
      }
      return true;
  }
  ```
  And similarly for `openTerminal`:
  ```cpp
  bool ViewerController::openTerminal(const QString& directory) {
      ...
      if (targetDir.isEmpty()) {
          d->status = "No folder is open to open a terminal.";
          emit stateChanged();
          return false;
      }
      const bool ok = glancelot::PlatformIntegration::openTerminal(targetDir);
      if (!ok) {
          d->status = "Could not open terminal.";
          emit stateChanged();
          return false;
      }
      return true;
  }
  ```
  - Status updates are now emitted and reflected in both the status bar and `statusToast`.
- **Status**: **RESOLVED**

### Finding 3: Explorer Fallback Argument Quoting
- **Resolution**:
  Changed fallback call to:
  ```cpp
  return QProcess::startDetached("explorer.exe", {"/select,", native});
  ```
  - Prevents argument quoting of `/select` when `native` has spaces.
- **Status**: **RESOLVED**

### Finding 4: Test Coverage
- **Resolution**:
  `tests/platform_integration_tests.cpp` expanded to cover:
  - `revealValidTempDirReturnsTrue`
  - `openTerminalRootDirectoryReturnsTrue`
  - `revealDirectoryVsFileBehaviors`
  - `pathsWithSpacesAndUnicodeValidation`
  `tests/grid_ui_tests.cpp` expanded to verify:
  - Empty path feedback on `revealInFileManager("")` and `openTerminal("")`.
  - Failure feedback on non-existent paths.
- **Status**: **RESOLVED**

---

## 4. Test Suite Execution Results

Executed clean full build and CTest run in `.worktrees/platform-ops-review` via:
`powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir build-platform-ops-review -Test`

### Test Output Summary
```text
Test project C:/src/Github/Glancelot/.worktrees/platform-ops-review/build-platform-ops-review
      Start  1: navigation
 1/14 Test  #1: navigation .......................   Passed    5.28 sec
      Start  2: directory_listing
 2/14 Test  #2: directory_listing ................   Passed   32.36 sec
      Start  3: file_operations
 3/14 Test  #3: file_operations ..................   Passed    0.26 sec
      Start  4: viewer_acceptance
 4/14 Test  #4: viewer_acceptance ................   Passed    6.95 sec
      Start  5: thumbnails
 5/14 Test  #5: thumbnails .......................   Passed    7.65 sec
      Start  6: folder_tree
 6/14 Test  #6: folder_tree ......................   Passed    8.97 sec
      Start  7: pane_layout
 7/14 Test  #7: pane_layout ......................   Passed    1.07 sec
      Start  8: window_slots
 8/14 Test  #8: window_slots .....................   Passed   31.83 sec
      Start  9: folder_grid
 9/14 Test  #9: folder_grid ......................   Passed  125.46 sec
      Start 10: file_operation_service
10/14 Test #10: file_operation_service ...........   Passed    1.26 sec
      Start 11: directory_model
11/14 Test #11: directory_model ..................   Passed   24.73 sec
      Start 12: viewport
12/14 Test #12: viewport .........................   Passed    0.10 sec
      Start 13: themes
13/14 Test #13: themes ...........................   Passed    0.35 sec
      Start 14: platform_integration
14/14 Test #14: platform_integration .............   Passed   23.37 sec

100% tests passed out of 14
Total Test time (real) = 270.37 sec
```

---

## 5. Invariant Checks

- **Worktree Isolation**: Verified. Review performed exclusively within assigned worktree `C:\src\Github\Glancelot\.worktrees\platform-ops-review`.
- **Shared ABI / Header Contract**: `include/glancelot/platform_integration.hpp` and `include/glancelot/viewer_controller.hpp` adhere to Qt/QML invocation guidelines.
- **Process Safety**: Processes are started detached without parent process handle leakage or lingering blocking calls.
- **Shortcut Precedence**: Modal dialogs and text editing inputs correctly override and suppress shortcuts.
- **Source Safety**: Viewing/navigating/revealing never writes or mutates media files or source directories.

---

## 6. Recommendation

**APPROVE**

Commit `cf1a97f` resolves all findings from the initial review. The feature satisfies all requirements of **IN-01**, **SYS-04**, and `docs/WORK-QUEUE.yaml` acceptance criteria. Task `PLATFORM-OPS-01` is ready for integration into `main`.
