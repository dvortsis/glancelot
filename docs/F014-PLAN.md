# F014 (partial) contract and delivery: thumbnail badges

Lane Claude Code; branch `claude/f014` (stacked on `claude/f009`); reviewer from the Codex lane.

## Delivered (from data the grid already has)
- Type badge: the file extension (for example PNG) at the top left of a photo tile, shown only when the preview is at least 96 px wide.
- Failed-preview badge: an alert icon plus the words "No preview" at the bottom left, driven by the image's real load status; the label drops out at narrow widths and the icon remains. Never colour alone.
- Placement keeps clear of the link badge (top right), the "Viewing" tag and the file name; a test asserts the two left badges never overlap.

## Not done (gated on META-HUD-01 and the metadata scheduler)
Rating, dimension and preview-quality badges. They need lazy metadata, and no QML delegate may read metadata itself.
