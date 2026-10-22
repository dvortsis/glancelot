# F031 contract and delivery: slideshow

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f031` (stacked on `claude/f037`); build `build-claude-f031`; reviewer from the Codex lane.

## Frozen interface (approved additions)
Registry, scope `canvas`, unbound: `slideshow`, `slideshow-slower`, `slideshow-faster`. No header change.

## Behaviour
- One timer, 1 to 60 seconds per photo (default 5, session only). It advances only once the current photo has shown, so a slow decode never builds a queue of steps.
- It honours the folder boundary mode: a step that goes nowhere (stop boundary) ends it.
- It ends on any manual navigation (keys, toolbar, wheel detent), Escape (first), opening a modal, window deactivation or entering the grid.
- The status bar shows "Slideshow · N s" while it runs.

## Not done
Persisting the pace, transitions, shuffle order, and a hidden-cursor full-screen presentation mode.
