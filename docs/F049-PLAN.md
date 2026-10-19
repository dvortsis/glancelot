# F049 (focus-mode part) contract and delivery

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f049` (stacked on `claude/f040`); reviewer from the Codex lane.

## Delivered
Registry command `focus-mode` (global, unbound): hides the top bar, status bar, filmstrip and folder tree for the session without touching the saved layout (tested: `paneLayout.panes` is identical before and after). Escape returns the bars (after a pending chord and a running slideshow, which take it first). Frameless and full-screen windows keep their own title handling.

## Not done (gated on UI-SETTINGS-01)
Named layout presets with persistence and their settings UI. They need the settings dialog that the Gemini lane is still changing, so they wait for that task to integrate.
