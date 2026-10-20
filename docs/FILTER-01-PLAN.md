# FILTER-01 Implementation Plan

Task: `FILTER-01`
Requirements: `NAV-05`, `UX-02`, `META-06`
Feature: Temporary vs. Persistent Folder-File Filtering
Owner: Gemini lane (`agent/filter`)
Worktree: `.worktrees/filter`
Build: `build-filter`

---

## 1. Executive Summary & Goals

`FILTER-01` implements requirement **NAV-05** and **UX-02** from `docs/REQUIREMENTS.md`:
- **NAV-05**: "Clear temporary filters on successful directory crossing: Candidate search excludes temporary filter, commit clears it; failure preserves original state; persistent filters and tags survive."
- **UX-02**: "Top HUD / Grid: filmstrip, filters, sort; controls operate by keyboard; preview generation does not stall canvas."

### Key Invariants:
1. **Explicit Temporary vs. Persistent Modes**:
   - A filter has a pattern string and a persistence flag: `Temporary` (default session filter) or `Persistent` (pinned across folders).
2. **Directory Crossing Semantics**:
   - Stepping within a folder obeys the active filter (both traversal and grid agree).
   - When crossing into an adjacent folder or navigating to a new folder:
     - Cross-folder candidate search evaluates directories without being blocked by temporary filters.
     - Upon successful commit to a new directory, temporary filters are automatically cleared, resetting the view to the full directory listing.
     - If candidate search fails (e.g. boundary reached, no more folders, access denied), original folder state, selection, and active filter are completely preserved.
   - Persistent filters survive successful directory crossings intact.
3. **Synchronization between Grid & Traversal**:
   - `DirectoryEntryModel` and `ViewerController` share filter state and signals so grid items, filmstrip, and Next/Previous navigation remain perfectly synchronized.
4. **UI Affordance**:
   - Filter bar/indicator in the grid and top bar showing active filter, match count, clear affordance (`Esc` / `[x]`), and toggle for Temporary vs. Persistent (`pin` icon).

---

## 2. Shared Contracts & Architecture

### 2.1 Filter State Contract (`include/glancelot/filter_state.hpp`)
```cpp
namespace glancelot {

enum class FilterPersistence {
    Temporary,
    Persistent
};

struct FilterSpec {
    QString pattern;
    FilterPersistence persistence{FilterPersistence::Temporary};
    bool caseSensitive{false};

    bool isActive() const { return !pattern.trimmed().isEmpty(); }
    bool isPersistent() const { return persistence == FilterPersistence::Persistent; }
    bool matches(const QString& text) const;
    void clear() { pattern.clear(); }
};

} // namespace glancelot
```

### 2.2 Integration in `DirectoryEntryModel`
- Properties:
  - `QString filterPattern` (READ filterPattern WRITE setFilterPattern NOTIFY filterChanged)
  - `bool filterPersistent` (READ isFilterPersistent WRITE setFilterPersistent NOTIFY filterChanged)
  - `bool hasFilter` (READ hasFilter NOTIFY filterChanged)
  - `int filteredCount` (READ filteredCount NOTIFY filterChanged)
- Methods:
  - `Q_INVOKABLE void setFilter(const QString& pattern, bool persistent = false);`
  - `Q_INVOKABLE void clearFilter();`
  - `Q_INVOKABLE void toggleFilterPersistence();`

### 2.3 Integration in `ViewerController`
- Synchronizes with `DirectoryEntryModel`:
  - Tracks active filter specification.
  - In `navigate()` / boundary crossing:
    - If moving across directory boundary and crossing succeeds: clears temporary filter.
    - If crossing fails: retains filter.
  - Exposes `setFilter()`, `clearFilter()`, `filterPattern`, `filterPersistent`, `hasFilter`.

### 2.4 UI Integration
- Grid / Header search pill with Pin icon to toggle persistence.
- Shortcut `Ctrl+F` focuses filter bar in grid; `Esc` clears filter.

---

## 3. Test & Verification Plan (`tests/filter_tests.cpp`)
1. Filter matching: substring, case-insensitivity, empty string.
2. Grid filtering: row count updates, non-matching rows hidden, selection preserved on matching items.
3. Intra-folder traversal: Next/Previous steps only through filtered items.
4. Crossing success: temporary filter clears when stepping into next folder; persistent filter stays active.
5. Crossing failure: boundary stop retains temporary filter and selection.
6. Full test run on Windows (`scripts/build.ps1 -Test`) and Docker Linux container.
