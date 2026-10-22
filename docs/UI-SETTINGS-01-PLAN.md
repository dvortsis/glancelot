# UI-SETTINGS-01 Task Packet & Plan

- **Task**: `UI-SETTINGS-01`
- **Features**: `F050` (Searchable settings, changed-from-default view and import preview), `UX-17` (Advanced UI customization and settings), `IN-06`, `UX-06`, `UX-07`, `UX-11`, `SYS-03`
- **Owner**: Gemini lane (`agent/ui-settings`)
- **Worktree**: `.worktrees/ui-settings`
- **Build Directory**: `build-ui-settings`
- **Status**: Contract Frozen

---

## 1. Scope and Requirements

`UI-SETTINGS-01` delivers comprehensive, accessible settings management for Glancelot:
1. **Searchable Settings & Navigation**:
   - Universal search field across all dialog sections and settings rows (not just controls/commands).
   - Dynamic filtering of pages, sections, and individual setting rows matching search text.
   - Changed-from-default visual indicators and reset-to-default affordances per row/section.
2. **Settings Import, Export & Reset**:
   - Portable JSON import and export with schema validation (`glancelot::exportSettings`, `glancelot::importSettings`).
   - Preservation of unknown keys, window slots, and active themes/bindings across imports and edits.
   - Per-section reset (`glancelot::resetSection`) for Appearance, Viewing, Browsing, Panes, and Bindings.
   - Full settings reset (`glancelot::resetAllSettings`).
   - Clear user feedback and error messages without dialog crashes or data loss.
3. **Accessibility & Customization Controls**:
   - UI text scale options (100%, 125%, 150%) adjusting `Theme.textScale` dynamically.
   - Reduced motion toggle disabling or minimizing non-essential animations.
   - Grid and file browsing customization controls (thumbnail buckets, hidden files toggle, folders first).
4. **Architectural & Safety Invariants**:
   - Uses atomic writes through `glancelot::updateSettings` and `.bak` last-known-good copies.
   - Never overwrites or downgrades newer schemas.
   - Clean keyboard-only navigation with full tab order and screen-reader accessibility roles.

---

## 2. Shared Contracts & Headers

### `include/glancelot/settings_file.hpp`
```cpp
namespace glancelot {
struct SettingsStatus { ... };
QString settingsDirectory();
QString settingsFilePath();
SettingsStatus readSettings(QJsonObject& out);
SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit, bool waitOnExit = false);
bool resetSettings(QString* movedTo = nullptr);

// Portable export, import, and section reset
bool exportSettings(const QString& targetFilePath, QString* error = nullptr);
bool importSettings(const QString& sourceFilePath, QString* error = nullptr);
bool resetSection(const QString& sectionName, QString* error = nullptr);
}
```

### `include/glancelot/viewer_controller.hpp`
```cpp
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY stateChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY stateChanged)

    Q_INVOKABLE bool exportSettings(const QString& targetFilePath);
    Q_INVOKABLE bool importSettings(const QString& sourceFilePath);
    Q_INVOKABLE bool resetSection(const QString& sectionName);
    Q_INVOKABLE bool resetAllSettings();
```

---

## 3. UI Design & Component Specifications

1. `ui/SettingsDialog.qml`:
   - Top-level global search bar: searches section titles, row labels, and descriptions.
   - Search result counter and active section highlights.
   - Export settings and Import settings actions with native file picker or prompt.
   - Section action buttons: "Reset section to defaults".
   - "About" / "Advanced" page with full reset option: "Reset all settings".
2. `ui/Theme.qml`:
   - Property `property real textScale: 1.0` modifying font pixel sizes proportionally.
   - Property `property bool reducedMotion: false` zeroing animation durations.
3. `ui/SettingRow.qml`:
   - Support optional `isModified` indicator or changed-from-default badge.
   - Search visibility filtering based on parent query.

---

## 4. Verification Plan

A new test executable `ui_settings_tests` (`tests/ui_settings_tests.cpp`) verifies:
1. `exportSettings`: exports valid JSON matching current settings schema.
2. `importSettings`:
   - Validates JSON format and schema version.
   - Rejects invalid/malformed JSON with clear error.
   - Merges valid settings cleanly, updating controller state.
   - Preserves unknown settings keys and window slots.
3. `resetSection`:
   - Resets specified section (e.g. `appearance`, `browsing`, `bindings`) to default without modifying other sections.
4. `resetAllSettings`:
   - Resets all configurable preferences to defaults while preserving schema integrity.
5. `textScale` and `reducedMotion`:
   - Bounds clamping and change notifications.
6. Full CTest suite and Docker build execution.
