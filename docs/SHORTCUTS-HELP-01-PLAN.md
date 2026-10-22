# SHORTCUTS-HELP-01 Implementation Plan

Task: `SHORTCUTS-HELP-01`
Requirements: `IN-01`, `IN-04`, `UX-13`
Feature: `F056`
Owner: Gemini lane (`agent/shortcuts-help`)
Worktree: `.worktrees/shortcuts-help`
Build: `build-shortcuts-help`

---

## 1. Executive Summary & Goals

`SHORTCUTS-HELP-01` implements feature `F056` from `docs/FEATURE-OPPORTUNITIES.md`:
"Small first-run hints, shortcut discovery and context-sensitive help | Stable workflows; skippable/replayable hints, no blocking tour, no repeated prompts and an offline help route."

The feature delivers a clean, responsive, offline keyboard shortcut cheat sheet and discovery modal:
- Fast access via `F1` and `?` (when not typing in an active text input), or from the command palette (`"Cheat Sheet: Keyboard Shortcuts"` / `"shortcuts-help"`).
- Categorized sections:
  1. **Navigation & Folders**: Next/Previous Photo, Next/Previous Folder, Back/Forward History (`Alt+Left`/`Alt+Right`), Open Image.
  2. **Canvas & Viewing**: Fit, Actual Size (1:1), Zoom In/Out, Rotate, Background Color, Fullscreen, Frameless.
  3. **Panes & Layout**: Folder Grid, Filmstrip, Folder Tree, Combined Grid+Preview, Pane Orientation, Command Palette, Settings.
  4. **Selection & Operations**: Select All, Clear Selection, Invert Selection, Thumbnail Zoom, Delete File/Folder/Selection, Copy Path/File/Image, Add to Favorites.
- Dynamic binding synchronization: looks up actual active bindings from `viewer.bindingFor(id)` so customized keybindings immediately reflect in the cheat sheet.
- Real-time instant search bar at the top: typing filters shortcuts across all categories with highlight matches.
- Direct bridge to Settings: button affordance to "Customize shortcuts in Settings..." which opens the Settings dialog directly to the Shortcuts tab.
- Standard dialog ergonomics: dismiss on `Escape`, `F1`, close button, or outside click.
- 100% offline, self-contained, lightweight, zero telemetry.

---

## 2. Shared Contracts & Architecture

### 2.1 Controller Command Registration (`src/app/viewer_controller.cpp`)
- Command added to the registry:
  ```cpp
  {"shortcuts-help", "Keyboard shortcuts cheat sheet", "F1"}
  ```
- Exposes `openShortcuts()` / `openSettingsTab(const QString& tabName)` or signal for clean UI activation.

### 2.2 QML Component (`ui/ShortcutCheatSheet.qml`)
- Dialog modal based on `UiDialog` with custom width/height (approx 780x560, responsive).
- Top search field using `UiTextField` with search icon and clear button.
- Grid / Flow of categories, each in a clean styled card (`Theme.palette.surface`, subtle border).
- Key badges rendered as styled keyboard keys with font tokens and high contrast.
- Filter logic: matches command title, description, or key sequence.
- "Customize Shortcuts" link/button in footer.

### 2.3 UI Integration (`ui/Main.qml`)
- Connects `shortcuts-help` command to opening `shortcutsCheatSheet`.
- Handles `?` key in root `Shortcut` or `Keys.onPressed` when active focus is not in a text field.

### 2.4 Test Strategy (`tests/shortcuts_help_tests.cpp`)
- Verifies `shortcuts-help` command is registered and bound to `F1`.
- Verifies categorized command map coverage (every registered interactive command is represented in a category).
- Verifies dynamic binding reflection: changing a binding updates the cheat sheet model.
- Verifies filtering / search logic.

---

## 3. Worktree & Lane Rules
- Branch: `agent/shortcuts-help`
- Worktree: `.worktrees/shortcuts-help`
- Build directory: `build-shortcuts-help`
- Reviews cross lanes: Claude or Codex review agent.
- Serial integration: acquire `integration_lock`, verify `scripts/build.ps1 -Test` and Docker build.
