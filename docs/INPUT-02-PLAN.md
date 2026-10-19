# INPUT-02 contract and delivery

The lead froze this contract on October 18, 2026.

| | |
| --- | --- |
| Lane | Claude Code |
| Requirements | IN-01, IN-02, IN-03, IN-05; adds F055 context menus |
| Branch | `claude/input-02` |
| Worktree | `.worktrees/claude-input-02` |
| Build directory | `build-claude-input-02` |
| Reviewer | Codex lane |

Use native Qt/QML with the existing Theme tokens and primitives, and apply the relevant ui-design-skills.

## Current state

- `registry` in `src/app/viewer_controller.cpp` is a flat command table. Each entry is a `Command` with `id`, `label`, `defaultBinding` and `repeat`.
- Bindings are one global map. `shortcutOverlap` and `usableBindings` reject any two sequences that overlap, including chord prefixes.
- `ui/Main.qml` creates one `Shortcut` per command. A shortcut is gated by:
  - `modalOpen`,
  - `editingText`,
  - `gridScope` together with `gridAllowsCommand`.
- The wheel handler is a fixed `wheelMode`, and Ctrl and Shift override it.
- No command has a scope, a hold lifecycle or a context-menu route.

## Frozen interfaces (pre-approved, exactly as written)

### `include/glancelot/contracts.hpp`

Append two defaulted members to `Command`. Existing positional initializers stay valid.

```cpp
struct Command { std::string id, label, defaultBinding; bool repeat{};
                 std::string scope{"global"};   // "global" | "workspace" | "canvas" | "grid" | "tree"
                 bool hold{}; };                // press activates; release, focus loss, pointer cancel or image change restores
```

### `include/glancelot/viewer_controller.hpp`

These additions are approved:

```cpp
Q_PROPERTY(QVariantMap wheelModifiers READ wheelModifiers NOTIFY stateChanged)
QVariantMap wheelModifiers() const;                                    // {"none","ctrl","shift","alt"} -> action
Q_INVOKABLE bool setWheelModifier(const QString& modifier, const QString& action); // "navigate"|"zoom"|"pan"|"pan-horizontal"
Q_INVOKABLE QString bindingConflict(const QString& id, const QString& sequence) const; // conflicting command id or ""
```

- Each `commands` entry gains `scope` and `hold` keys. This needs no header change.
- `wheelMode` keeps its meaning as the "none" modifier entry.

### Persistence

Wheel modifiers are stored under `"wheelModifiers"` through the existing locked settings writer. Unknown or invalid entries fall back individually, and unknown keys are kept.

## Behaviour

- **Scope resolution:**
  - Precedence, highest first: modal, text editing, focused pane (grid, tree or canvas), workspace (canvas or grid content), global.
  - The same sequence may be bound in scopes that do not overlap, for example Delete in the grid and in the canvas. "global" overlaps every scope, and "workspace" overlaps "canvas" and "grid".
  - `shortcutOverlap`, `usableBindings` and `setBinding` become scope-aware, and `bindingConflict` reports the blocking command.
  - `gridAllowsCommand` is replaced by scope data. The current grid allow-list becomes the scope of each command, and behaviour does not change.
- **Chords:**
  - Two-step sequences such as `G, D` are allowed. A prefix conflict inside overlapping scopes is rejected.
  - Pending state times out after 1500 ms, or is cancelled by Escape, focus loss or a modal opening.
  - A small, unobtrusive pending-chord indicator appears in the status bar.
  - The first key of a chord never also triggers a single-key command in the same scope.
- **Holds (IN-05):**
  - A command with `hold` activates on press and restores on release.
  - It also restores on window deactivation, pointer cancel, image change or modal open.
  - Auto-repeat while held does nothing.
  - First hold command: `hold-actual`, "Show actual size while held", default `Z`, scope "canvas". It restores the previous fit or zoom exactly.
- **Wheel matrix (IN-03):**
  - Each modifier maps to one action.
  - Defaults reproduce today's behaviour: none follows `wheelMode`, Ctrl zooms, Shift pans.
  - High-resolution trackpad deltas accumulate to detent steps for navigate, and are continuous for pan and zoom.
  - Zoom stays cursor-centred, and horizontal pan works.
- **Context menus (F055):**
  - Menus exist for the canvas, a grid item or empty area, a tree row and the path bar.
  - They open on right-click, the Menu key or Shift+F10.
  - Every item is a registered command, shown with its live binding. Enablement uses the same predicate as the shortcut, so the menu and keyboard never disagree.
  - A destructive item names its target scope, for example "Move 3 items to the Recycle Bin".
  - Compact widths keep the menus reachable.
- **Command-table hygiene:** new commands go in a block commented `// INPUT-02`.

## Boundaries

- Do not touch the Gemini-lane features: UI-SETTINGS-01 (shortcut editing UI), F056 (cheat sheet), F004, F010 and F011.
- Keep changes to `PhotoGrid.qml` and the directory model minimal and well separated, because Gemini is editing them.
- The shortcut recorder (F051) is a later Claude packet after UI-SETTINGS-01.

## Verification

- **Controller tests in `tests/window_slot_tests.cpp`, or a new core-controller test:**
  - scope-aware conflict detection and `bindingConflict`;
  - a chord prefix rejected only in overlapping scopes;
  - wheel-modifier persistence, invalid fallback and the cross-window merge.
- **UI tests:** a new file `tests/input_ui_tests.cpp`. Its CMake target `input_ui_tests` (`add_test(NAME input_ui ...)`) is pre-approved and mirrors `grid_ui_tests`. It covers:
  - the precedence matrix: modal, text, then each pane, for the same key;
  - chord timeout, Escape and focus-loss cancellation;
  - the `hold-actual` restore on each of release, focus loss, image change and modal open;
  - auto-repeat ignored while a hold is active;
  - every wheel modifier, with detent and high-resolution delta sequences;
  - context-menu items matching the shortcut's enabled state, plus keyboard opening of every menu;
  - dark/light and compact/wide captures behind `GLANCELOT_REVIEW_CAPTURE`.
- **Full suite and Linux:** `scripts/build.ps1 -BuildDir build-claude-input-02 -Test`, plus the Linux Docker build.
- **Unverified unless measured:** physical trackpads and real keyboard layouts or IMEs.
