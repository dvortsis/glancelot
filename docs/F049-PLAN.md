# F049 (focus-mode part) contract and delivery

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f049` (stacked on `claude/f040`); reviewer from the Codex lane.

## Delivered
Registry command `focus-mode` (global, unbound): hides the top bar, status bar, filmstrip and folder tree for the session without touching the saved layout (tested: `paneLayout.panes` is identical before and after). Escape returns the bars (after a pending chord and a running slideshow, which take it first). Frameless and full-screen windows keep their own title handling.

## Not done (gated on UI-SETTINGS-01)
Named layout presets with persistence and their settings UI. They need the settings dialog that the Gemini lane is still changing, so they wait for that task to integrate.

## Layout presets (second delivery)

`PaneLayout` gains additive API: `presetNames`, `isBuiltInPreset`, `savePreset`, `applyPreset`, `deletePreset`, signal `presetsChanged`. Three built-ins (Default, Review: photo plus filmstrip under it with tree and toolbar hidden, Browse: combined grid and photo) and up to 12 user presets stored once for all windows under `layoutPresets`. A preset holds pane regions, order, extent, visibility, the centre split and the content mode; never lock or freeze. Applying is one step, persists for the window, and is refused with an explanation while the layout is locked or any pane is frozen. Names are 1 to 40 characters with no padding and cannot reuse a built-in name. Settings → Browsing has the chooser, Apply, Delete and "Save current layout as"; the command `layout-preset-next` cycles through every preset from the keyboard. Tests: `pane_layout_tests` (layoutPresets) and `input_ui_tests` (command and Settings). Not done: renaming, reordering, per-monitor presets, sharing a preset file.
