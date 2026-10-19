# F013 and F042 UI: proposed packets for the lead to freeze

Status: PROPOSAL by the Claude lane, October 19, 2026. Not frozen; nothing here is implemented. A row becomes active only when the lead freezes these interfaces and promotes it in `docs/WORK-QUEUE.yaml`. Gates, from the lane plan: META-HUD-01 (metadata service, Gemini), F042 core (Codex: capture-date and dimension sorts through that service) and the directory model from GRID-DENSITY-01 / F004 (integrated).

## What the UI needs from the metadata work (read-only contract)
1. `DirectoryEntryModel` roles, filled lazily by the metadata service and never by a QML delegate: `captureTime` (QDateTime, invalid when unknown), `pixelWidth`, `pixelHeight` (0 when unknown), `rating` (0 to 5, -1 when unknown), `metaState` ("unknown" | "pending" | "ready" | "failed"). Unknown must be an explicit state so the UI can show a group or chip for it.
2. `DirectoryEntryModel` sort keys `captured` and `dimensions` (F042 core), missing values last, stable ties.
3. A bounded request surface: `requestMetadata(firstRow, lastRow)` for the visible window only, cancelled when the window or folder changes. The UI never asks per item.
4. Generation numbers on results, as for thumbnails; a stale batch cannot overwrite a newer listing.

## F013 group headers (Claude UI)
- Group modes: none, type (folders, photos, other files), date (day, month, year by sort), rating. Missing values form an explicit "Unknown" group, last.
- A header row shows name and item count; Enter or click collapses it. A collapsed group is not rendered, requests no thumbnails and keeps its items in traversal order, so photo stepping is unchanged.
- Headers are real model rows (kind `header`) emitted by a grouping proxy, not decoration, so selection, cursor keys, Home/End, type-to-select and screen readers treat them as headings that the cursor skips. Row identity stays the path; a header never carries a path.
- Commands in a `F013` registry block (scope `grid`): `group-by-none`, `group-by-type`, `group-by-date`, `group-by-rating`, `group-collapse-all`. Mode persists per window slot like sort.
- Verification: groups with 100000 entries stay within the model update budget, collapse and relist keep the cursor item, empty and unknown groups, keyboard and accessibility names, dark/light and 540x360 captures, no thumbnail request for a collapsed group.

## F042 UI metadata filter chips (Claude UI)
- A chip row above the grid: file type, rating (at least), orientation, date range. Each chip shows its count and a clear action; chips combine with AND, an "Unknown" value is a chip of its own, and a filter never changes folder traversal unless the user chooses "browse filtered" (explicit).
- Filtering runs in the model on the worker, with the same generation rule as listing; the UI shows "Filtering..." and never blocks on metadata that is still pending (pending items stay visible, marked, until proven not to match).
- Commands (`F042` block, scope `grid`): `filter-clear`, `filter-toggle-chips`. Chip state is session state in the first delivery.
- Verification: pending, failed and unknown metadata, rapid chip changes discarding stale results, empty result state with a clear action, keyboard reachability of every chip, text fields keep shortcut precedence.

## Boundaries
No UI change until the roles above exist. The proposal touches `PhotoGrid.qml` only through new components (a header row delegate and a chip bar) and one hook each, because the Gemini lane edits that file.
