# GRID-PREVIEW-01 contract and delivery

The lead froze this contract on October 18, 2026. Requirements: UX-15, IN-02, PERF-04 and SYS-02. It depends on PANES-01 (done) and FILE-GRID-01.

Implementation runs on branch `agent/grid-preview` from `main` once FILE-GRID-01 is integrated, in worktree `.worktrees/grid-preview` with build directory `build-grid-preview`. Use native Qt/QML, the unchanged GPU `ImageCanvas`, and the existing Theme tokens and UI primitives. Apply the relevant ui-design-skills and run an independent native design-review.

## Frozen interfaces

These additions to `PaneLayout` (`include/glancelot/pane_layout.hpp`) are pre-approved, exactly as written:

```cpp
Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)   // "preview" | "grid" | "combined"
QString contentMode() const;
Q_INVOKABLE bool setContentMode(const QString& mode);              // false for unknown values; no-op when equal
```

- The mode is stored in this slot's center object as `"mode"` and persisted through the existing per-slot merge (field-level, under the writer lock). Slot 0 keeps it for the session only.
- An unknown or invalid stored mode falls back to `"preview"` individually, and other center fields are kept.
- `reset()` restores `"preview"`.
- Content mode is a view preference, not geometry. It may change while the layout is locked or the center is frozen; the split ratio and orientation may not.

Everything else uses the existing `centerSplit` (`orientation` "horizontal"/"vertical", `ratio`), `DirectoryEntryModel` and `ViewerController` APIs. Any further shared-header change goes through the lead.

## Behaviour

- **Modes:**
  - **Preview only:** today's canvas.
  - **Grid only:** today's grid mode.
  - **Combined:** the grid and the preview share the center, using the stored orientation and ratio. Horizontal means side by side; vertical means stacked. The edge placement that PANES-01 saved for the grid applies to which side or end it takes.
- **Commands:** the commands below are remappable and appear in command search and Settings. The top-bar control shows all three modes as one segmented choice, which compacts to a menu on narrow windows. The existing `grid` toggle keeps its meaning (grid-only versus preview-only).

  | Command | Default binding |
  | --- | --- |
  | `view-preview` | none |
  | `view-grid` | none |
  | `view-combined` | Ctrl+Shift+G, if free (check for conflicts) |
  | `combined-orientation` | none |
- **Selection and preview in combined mode:**
  - When the grid cursor moves onto an image, the preview updates through `viewer.open(path)` without leaving the grid or moving focus. Debounce this by 120 ms so a held arrow key does not queue a decode per step. The decode generation already drops stale results; the newest cursor item wins.
  - Multiselection does not change the preview; only the cursor item does.
  - The opened image (the "Viewing" tag) is the preview's image, and it is distinct from the cursor and from the selection.
  - Enter on an image in combined mode moves focus to the preview, and does not leave combined mode. Enter on a folder enters it.
- **Explicit preview states:**
  - Folder under the cursor: the folder name, its item count if the grid knows it, and "Press Enter to open".
  - Unsupported file: the generic file icon, the extension and "Glancelot can't preview this file type". It is never decoded.
  - No selection, empty folder or inaccessible folder: a matching state, never a stale image presented as current.
  - The last opened image is not shown as if it belonged to a folder or file cursor.
- **Focus and precedence:** order is modal, then text, then the focused pane. Arrow keys, Home and End act on whichever of the grid or the preview has focus. Tab and F6 move between the grid and the preview, and the focused pane shows the focus ring. Canvas-only commands (zoom, pan, fit and rotate) apply in combined mode when the preview has focus or is invoked explicitly. Grid commands apply when the grid has focus.
- **Resize:** dragging the split handle changes `ratio`, which is persisted once on release, with a keyboard equivalent. Grid and preview minimum extents are respected. A compact window falls back to the PANES-01 responsive recovery, either a single pane with a notice or stacked, without persisting the fallback. During resize the canvas keeps fit mode, or the physical zoom when the user zoomed. No decode is issued from resize handlers.
- **Bounds:** in combined mode the thumbnail requests and the single current decode share the existing service limits. Do not raise the thumbnail queue or the decode concurrency. Hidden content releases its work, as today: grid-only cancels canvas lookahead, and preview-only deactivates the grid model.
- **Restoration:** the mode, orientation and ratio restore per slot on restart. Combined mode with no folder opens with the grid's no-folder state and an empty preview.

## Verification

Use generated fixtures only.

Layout tests (`pane_layout`):

- mode round-trips per slot;
- an unknown mode falls back;
- the mode is allowed under global lock while the ratio is not;
- reset restores preview;
- concurrent merges keep the other window's mode.

UI tests (`grid_ui_tests`):

- all three modes and switching between them by command and by pointer;
- a cursor move updates the preview after the debounce, and rapid moves issue no more than a bounded number of decode requests, with the newest winning;
- a folder or unsupported cursor shows the explicit state and never decodes;
- multiselection does not change the preview;
- focus moves with Tab/F6, and command precedence is checked with a modal open and with text focus;
- horizontal and vertical splits, plus a ratio drag and its keyboard equivalent;
- a folder change while a preview decode is pending discards the stale result;
- restart restores the mode;
- compact recovery does not persist;
- fit and physical zoom survive a split resize;
- dark/light, compact/wide and horizontal/vertical captures behind `GLANCELOT_REVIEW_CAPTURE`.

Run the full suite with `scripts/build.ps1 -BuildDir build-grid-preview -Test`, and run Linux through the Docker image.
