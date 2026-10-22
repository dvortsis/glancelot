# F002 contract and delivery: breadcrumbs and path completion

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f002` (stacked on `claude/input-02`); build `build-claude-f002`; reviewer from the Codex lane.

## Frozen interface (approved additions)
- `ViewerController::completePath(const QString& text)` and signal `pathCompletions(text, names)`. One bounded folder listing runs off the UI thread; a newer request cancels the older one and only the newest result is emitted. Names are child folders of the typed scope that start with the last typed segment, at most 20, sorted case-insensitively.
- New test target `pathbar_ui_tests` (shared harness in `tests/ui_test_support.hpp`).

## Behaviour
- Breadcrumbs above the address field: one button per ancestor of the current folder (drive, UNC share and `/` roots kept), built from text only. A click navigates exactly like Enter in the field; the current folder is the last crumb and stays scrolled into view.
- Completion appears only while the address field has focus, for the typed scope only. Click or Tab accepts the first or clicked suggestion and appends a separator. Escape or leaving the field hides it and nothing is navigated or created.
- Exact text semantics are unchanged: pasting, Unicode, spaces, UNC and long paths pass through unmodified.

## Not done
Sibling jump menus on crumbs, arrow-key selection inside the suggestion list, and physical-share latency measurements.
