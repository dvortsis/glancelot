# F009 contract and delivery: details (list) mode

Lane Claude Code; branch `claude/f009` (stacked on `claude/f049`); reviewer from the Codex lane. Written against the existing `DirectoryEntryModel` roles (size, modified); no model change.

## Delivered
Registry command `view-details` (scope `grid`, unbound) toggles `PhotoGrid.details`. In details mode the grid is one column of rows: kind icon, name (middle-elided), size, and modified time (hidden below 520 px). The thumbnail tile lives in a Loader component, so details mode requests no thumbnails and builds no preview images. Selection, cursor keys, Enter, context menu and delete targets are the same code as tile mode (grid_ui_tests 72/72 unchanged; one new input_ui case).

## Not done
Sortable column headers, column widths and persistence of the mode. These belong with the sort and settings work (F010/F011 integrated into main as GRID-DENSITY-01 after this branch was cut; reconcile at integration).
