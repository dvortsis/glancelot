# F015 tabs and sessions: proposed packet for the lead to freeze

Status: PROPOSAL by the Claude lane, October 19, 2026. Not implemented: it changes how the application holds browsing state, which is a shared-contract decision.

## Why this needs a frozen contract first
Everything a viewer shows today hangs off one `ViewerController` (folder, current image, sort, navigation snapshot, decode worker, watcher) plus per-window models (`DirectoryEntryModel`, `FilmstripModel`, `FolderTreeModel`) wired in `app/main.cpp` and the test harnesses. A tab is another independent copy of that state. Three shapes are possible; the lead must choose one.

| Shape | What it is | Cost and risk |
| --- | --- | --- |
| A. One controller, tab list of locations | A tab is only {folder, image, sort}; switching tabs re-navigates the single controller | Small change; switching is a navigation (not instant) and loses the tab's scroll, selection and decoded image |
| B. A controller per tab (recommended) | Each tab owns a `ViewerController`, `DirectoryEntryModel` and `FilmstripModel`; the window shows one and keeps the rest idle | `Main.qml` binds to a tab-scoped context instead of the `viewer` global; bounded work for hidden tabs is required (no decode, no thumbnails, watcher paused) |
| C. Multiple windows only | No tabs; session restore already exists per window slot | No work, but not the feature |

## Proposed behaviour (shape B)
- Tabs bar above the content, hidden while there is one tab. Commands (scope `global`): `tab-new`, `tab-close`, `tab-next`, `tab-previous`, `tab-reopen-closed`, `tab-duplicate`. Ctrl+T, Ctrl+W and Ctrl+Tab are the natural defaults but must not clash with existing bindings.
- A hidden tab does no decoding, requests no thumbnails and pauses its folder watcher; its last image stays in memory only within the existing cache budget. At most 8 tabs.
- Sessions: the window's tab list (folder, image, sort per tab, active tab) persists under the window slot like position memory does today, written through the shared locked settings writer and restored lazily (only the active tab navigates at start).
- Context menu and drag: a folder tile or tree row gets "Open in new tab"; drag-reorder tabs by pointer and with Alt+Shift+arrows.
- Accessibility: tab strip is a real tab list with names, selected state and a close button per tab; closing never loses a pending delete confirmation.

## Verification to require
Two tabs with different folders keep independent selection, scroll and sort; hidden tabs run no decode or watcher (counted); closing and reopening restores the same state; restart restores the saved tab set with one navigation; delete reconciliation in one tab does not disturb another that shows the same folder; stale-result generations stay per tab; memory is bounded with eight 60 MP tabs.

## Dependencies
Wave 7 lists it after UI-SETTINGS-01 (done). It should land after the Codex PERF-RESOURCE accounting packet or carry its own admission limits, because hidden tabs multiply cache pressure.
