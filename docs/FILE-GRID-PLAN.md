# FILE-GRID-01 contract and delivery

Lead freeze, October 18, 2026. Branch `agent/file-grid` (from `agent/panes` 5c3f092, because both tasks reshape Main.qml and grid geometry). Worktree `.worktrees/file-grid`, build `build-file-grid`. Native Qt/QML, existing Theme tokens and UI primitives (UiButton, UiIcon, UiScrollBar), existing ThumbnailService/`image://thumb` provider.

## Frozen interfaces

- `include/glancelot/directory_listing.hpp`: the core `listDirectory()` contract (worker-only, cancellable, bounded at 100,000 entries).
- `include/glancelot/directory_entry_model.hpp`: the `DirectoryEntryModel` QML model contract.
- CMake targets `directory_listing` (core) and `directory_model` (Qt) tests. The model is compiled into `glancelot` and `grid_ui_tests`.

Change either header only through the lead. Adding private members, helpers or new source files is fine.

## Behaviour

- Entries: folders first in natural order (`naturalLess`), then all regular files ordered by the existing `SortSpec`/`fileSortBefore` (the viewer's sort key and direction). Hidden entries are excluded by default; the setting for them belongs to UI-SETTINGS-01. Special files are omitted.
- Identity: the stable key is the cleaned absolute path, case-folded on Windows. Selection, anchor and cursor are keys; rows are transient addresses.
- Type: an image is `isSupportedImage` plus a regular file. Everything else is `File`. Unsupported files never reach the decoder, thumbnail service or OS launch APIs. They show a generic file icon and an extension label.
- Links: directory links and junctions are listed with a link badge. Entering one through the grid follows the same policy as the tree and path bar: use `viewer.navigateDirectory`, which already validates. Automatic traversal never follows links.
- Async: listing runs on a worker with a monotonically increasing generation. Only the newest result applies. `setActive(false)` cancels, drops the listing and stops thumbnail work. Folder changes on disk (`viewer.filesInvalidated`) trigger `refresh()` and `invalidate()`. Results must not restore entries removed by a newer listing.
- Activation: Enter or double-click on a folder calls `viewer.navigateDirectory(path)`. On an image it opens the image (existing behaviour). On an unsupported file it does nothing beyond an inline/status explanation. Backspace and Alt+Up navigate to the parent folder via `navigateDirectory`. The tree and path bar stay synchronized because all of these go through the controller.
- Selection: click = Replace; Ctrl+click = Toggle; Shift+click = Range from anchor; Shift+arrows/Home/End/PgUp/PgDn extend; Ctrl+A select all; Escape clears a multi-selection first, then dismisses the grid as today. Ctrl+Space toggles the cursor item. A visible selection count appears when more than one item is selected. Selection identity survives relisting, sorting and a refresh.
- The grid replaces PhotoGrid's model source (`filmstrip`) with the new model. The filmstrip, canvas traversal and `FilmstripModel` keep eligible-image semantics unchanged. The opened image is `currentImage` (the "Viewing" tag); the cursor and selection are separate.
- States: loading, inaccessible ("can't open this folder" with a reason), empty folder, truncated ("showing the first 100,000 items"), and preview failure per tile.
- Bounds: delegates stay virtualized and thumbnails are requested only for visible image tiles. Respect the existing column cap and the 256-item queue. No synchronous stat or listing on the GUI thread.

## Verification

Generated fixtures only. Core tests: folders-first natural order; mixed supported and unsupported files; hidden exclusion; links (Linux symlink; Windows junction where creatable, otherwise reported unverified); permission-denied directory; cancellation; truncation with a small limit; sort keys. Model tests: newest generation wins (two rapid `setDirectory` calls); selection survives relist and drops removed keys; range/toggle/select-all/clear; `thumbnailSource` empty for non-images; inactive releases rows; 10,000+ entries listing stays bounded and responsive. UI tests in `grid_ui_tests`: folder tiles plus image and generic tiles render; Enter on a folder navigates and the tree/path bar follow; Backspace goes to the parent; unsupported Enter does not decode; Ctrl/Shift selection with the count label; external rename/delete then refresh; dark/light compact/wide captures behind `GLANCELOT_REVIEW_CAPTURE`. Full suite: `scripts/build.ps1 -BuildDir build-file-grid -QtRoot C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64 -Test`.
