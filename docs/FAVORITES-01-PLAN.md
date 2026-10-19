# FAVORITES-01 Task Packet & Plan

- **Task**: `FAVORITES-01`
- **Features**: `F003` (Folder favorites, pinned locations with optional aliases, missing mount recovery, keyboard reordering and navigation), `UX-03` (Collapsible left HUD: live directory tree and favorites), `SYS-03` (Portable configuration without destructive migration)
- **Owner**: Gemini lane (`agent/favorites`)
- **Worktree**: `.worktrees/favorites`
- **Build Directory**: `build-favorites`
- **Status**: Contract Frozen

---

## 1. Scope and Requirements

`FAVORITES-01` delivers folder favorites in the collapsible left folder tree HUD with settings persistence and missing-path recovery:
1. **Favorites Model & Persistence**:
   - `FavoritesModel` (`include/glancelot/favorites_model.hpp`, `src/app/favorites_model.cpp`) backed by `"favorites"` array in `settings.json`.
   - Each entry stores `path` (absolute path) and optional `alias` (custom label; empty string defaults to base folder name).
   - Atomic settings persistence through `glancelot::updateSettings`, preserving unknown keys, window slots, and concurrent modifications.
2. **Missing Mount Recovery & Zero Startup Probing**:
   - Invariant: "avoid recursive startup scans, probing all favorites or large speculative cross-folder reads that wake unrelated disks" (`PERFORMANCE-STRATEGY.md`, `F003`).
   - No blocking filesystem calls or disk-waking probes at startup or model instantiation.
   - Disconnected/unplugged mounts remain safely in favorites; activating an inaccessible favorite delegates to `viewer.openFolder(path)` / `viewer.navigateDirectory(path)`, which surfaces honest user feedback without crashing or locking.
   - Inaccessible or missing paths can be renamed or removed at any time via context menu or keyboard.
3. **Ergonomic Tree HUD Integration**:
   - Collapsible "Favorites" section at the top of the left HUD in `ui/FolderTree.qml`.
   - Visual parity with native file-tree conventions: star/pin glyph, folder name or custom alias, current-folder indicator bar, elided text with full path tooltip.
   - Tree context menu integration: any folder row in the main tree provides "Add to Favorites" (or "Remove from Favorites" if already pinned).
   - Section header action: "Add current folder" button (`+`).
4. **Keyboard Routes & Reordering**:
   - Full keyboard navigation within the favorites list (`Up`, `Down`, `Home`, `End`, `Enter`, `Space`).
   - Reordering via keyboard: `Alt+Up` / `Alt+Down` (or `Ctrl+Up` / `Ctrl+Down`).
   - Quick removal via `Delete` / `Backspace`.
   - Context menu trigger via `Menu` / `Shift+F10`.
   - Focus traversal between Favorites section and main Folder Tree via `Tab` / `Shift+Tab` / arrow navigation.
5. **Architectural & Safety Invariants**:
   - Models detached when left HUD is hidden (`panel.visible == false`) to guarantee zero work while hidden.
   - Command registry entry for `add-to-favorites` in `ViewerController`.

---

## 2. Shared Contracts & Headers

### `include/glancelot/favorites_model.hpp`
```cpp
#pragma once
#include <QAbstractListModel>
#include <memory>

class FavoritesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString currentDirectory READ currentDirectory WRITE setCurrentDirectory NOTIFY currentDirectoryChanged)
public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        AliasRole,
        IsCurrentRole,
        IndexRole
    };
    Q_ENUM(Role)

    explicit FavoritesModel(QObject* parent = nullptr);
    ~FavoritesModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString currentDirectory() const;
    void setCurrentDirectory(const QString& dir);

    Q_INVOKABLE bool addFavorite(const QString& path, const QString& alias = QString());
    Q_INVOKABLE bool removeFavorite(const QString& path);
    Q_INVOKABLE bool removeAt(int index);
    Q_INVOKABLE bool moveUp(int index);
    Q_INVOKABLE bool moveDown(int index);
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE bool setAlias(const QString& path, const QString& alias);
    Q_INVOKABLE bool isFavorite(const QString& path) const;
    Q_INVOKABLE QString aliasForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE void reload();

signals:
    void countChanged();
    void currentDirectoryChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
```

---

## 3. UI Design Specifications (`ui/FolderTree.qml`)

1. **Favorites Section**:
   - Header with `UiIcon` star glyph, "Favorites" label, count badge, and collapse chevron.
   - Collapsible container with `ListView` using `favoritesModel`.
   - Row height matching `Theme.rowHeight`, hover highlight, current directory accent marker.
2. **Context Menu**:
   - On favorite items: "Open", "Set alias…", "Move up", "Move down", "Remove from Favorites", "Copy path".
   - On tree items: "Add to Favorites" / "Remove from Favorites".
3. **Alias Dialog / Prompt**:
   - Inline or lightweight dialog allowing user to set/clear custom alias for a favorite path.

---

## 4. Verification Plan

A new test executable `favorites_tests` (`tests/favorites_tests.cpp`) verifies:
1. Model initialization from `settings.json` (empty, populated, string vs object formats).
2. Adding favorites: duplicate prevention, path normalization, custom aliases.
3. Removing favorites: by path and by index, signal emission, count updates.
4. Reordering: `moveUp`, `moveDown`, `move`, boundary clamping.
5. Alias modification and clearing.
6. Current directory tracking (`IsCurrentRole` updates).
7. Settings file coordination: atomic updates, schema version retention, preservation of other keys.
8. Non-blocking missing path recovery: missing paths load and display without blocking or disk probes.
9. Windows CTest suite and Linux container build.
