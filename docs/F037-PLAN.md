# F037 contract and delivery: zoom presets, zoom lock and navigator

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f037` (stacked on `claude/f002`); build `build-claude-f037`; reviewer from the Codex lane. Requirements VIEW-02, VIEW-03.

## Frozen interface (approved additions)
- `ImageCanvas`: `zoomLock` property, `zoomTo(scale)`, `visibleImageRect()` and `centerOn(x, y)` (source pixels). No change to the render or decode path.
- Registry (block `F037`, scope `canvas`, unbound): `zoom-50`, `zoom-200`, `zoom-400`, `zoom-lock`, `navigator`.

## Behaviour
- Presets are physical scales (1.0 is one image pixel per device pixel) about the view centre; the quick loupe and `hold-actual` already cover 1:1.
- Zoom lock keeps a manual magnification across photos, centred and upright. A fitted view never becomes locked, and changing fit mode or pressing a fit command releases the magnification as before.
- The navigator is a map of the photo with the visible area outlined. It shows only while zoomed past the whole photo; click or drag recentres. It follows rotation because it works in source pixels.

## Not done
Thumbnail inside the navigator (needs a derived-image path through the canvas), per-monitor fractional DPI evidence, and navigator keyboard nudging.
