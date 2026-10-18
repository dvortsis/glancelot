# FILE-GRID-01 verification

Branch `agent/file-grid`, worktree `.worktrees/file-grid`, build `build-file-grid`. State: implementation complete. Independent review, integration and Linux functional checks are pending. Contract: docs/FILE-GRID-PLAN.md (lead freeze b7ddd04). The frozen headers `directory_listing.hpp` and `directory_entry_model.hpp` and the root CMake are unchanged.

## What was built

- `listDirectory()` (src/core/directory_listing.cpp) lists one level: folders first in natural order, then every regular file in the viewer's `SortSpec`. On Windows it uses a single `FindFirstFileExW` pass, so it needs no per-entry stat. Elsewhere it uses `directory_iterator` without `skip_permission_denied`.
  - An entry is an Image only if it is a regular, non-link file with a supported extension, exactly what `Navigator::open` admits. Links of every kind (symlinks, junctions, reparse points) are listed with `link=true` and are never Image.
  - Hidden entries (dot-names, and the Windows hidden attribute) are excluded by default. Devices, sockets and FIFOs are omitted.
  - A folder reached through a link is reported inaccessible. Listing is cancellable during enumeration and sorting. `limit` bounds memory and sets `truncated`.
  - The link and safe-path policy moved, unchanged, from navigation.cpp's anonymous namespace into the private header `src/core/link_policy.hpp`. Traversal and the listing now share one implementation.
- `DirectoryEntryModel` (src/app/directory_entry_model.cpp):
  - Workers: a heap `QThreadPool` runs at most 2 listings at once plus one pending request. Every request bumps the generation and stops the previous token, and only the newest result applies. Rows, keys and the key-to-row index are built on the worker; the GUI thread only swaps them in.
  - Refresh: a same-folder refresh is merged in place (removals, then insertions, then one ranged `dataChanged`) when the surviving order is unchanged, and resets otherwise. Scroll position and delegates survive an external change.
  - Selection, anchor and cursor are case-folded path keys. They are filtered against each new listing, so removed entries drop out and a stale result cannot restore them.
  - Inactive is the default. While inactive the model cancels its work, releases its rows and keeps only the keys.
  - Going up a level puts the cursor on the folder just left.
  - `thumbnailSource()` is empty for non-image rows.
- PhotoGrid.qml:
  - Tiles: folder tiles, image thumbnails, generic file tiles with an extension label, a link badge, and the existing "Viewing" tag.
  - Header: an Up button, the folder name, an item count, and a "N selected" chip with a clear action. The chip appears only when more than one item is selected; the status bar mirrors the count.
  - States: loading, empty, can't-open (with a reason and an Up action), and truncated, plus a per-tile preview failure.
  - Activation: an inline notice explains unsupported files and folders the controller refused.
  - Placement is still `app.paneRect("grid")`. The model is active only while the grid is visible.
- app/main.cpp:
  - Registers `fileGrid`. It follows the controller's folder (by key, so stepping between images never relists), its sort and its opened photo.
  - `filesInvalidated` triggers a refresh. The grid's thumbnail epoch moves in lockstep with the filmstrip's, because `ThumbnailService` accepts a single epoch.

## Interpretations recorded

- `SelectMode::Range` (Shift+click, Shift+arrow/Home/End/PgUp/PgDn) replaces the selection with anchor..row. `ExtendRange` (Ctrl+Shift+click) adds anchor..row to the existing selection. Toggle (Ctrl+click, Ctrl+Space) moves the anchor and the cursor.
- Escape with more than one item selected clears the selection and keeps the cursor. Otherwise it dismisses the grid as before.
- Plain arrows move the cursor and select it (Replace). Ctrl+arrows (move without selecting) are not implemented because the frozen model has no cursor-only call.
- Enter acts on the cursor item. Folder: `viewer.navigateDirectory` (the grid stays open). Photo: open and return to the canvas. Other file: inline explanation only. The status-bar action reads "Open folder", "Open", or is disabled for a file.
- File symlinks and dangling links are listed as File with a link badge. A directory link or junction is listed as a folder with a badge. Entering one goes through `navigateDirectory`, which refuses links under the existing traversal policy, and the grid shows the controller's error inline.
- The cursor follows the viewed photo only when the grid is (re)entered and the viewed photo has changed since the last entry. While the grid is open, navigation keeps the cursor on the grid's own items.
- Cursor placement (on the viewed photo, the folder just left, or the first item) runs once per grid entry or folder change. An ordinary refresh caused by an external change never replaces the selection or scrolls the view. When the cursor item is deleted, the cursor becomes -1 and the next arrow key starts from the first item. `externalRenameAndDeleteRefreshKeepSelection` covers this.

## Acceptance coverage

| Item | Evidence |
| --- | --- |
| Folders first (natural), then all files in the viewer's sort; name/size/modified/shuffle; descending | `directory_listing_tests` (ordering, sort keys); `directory_model_tests::foldersFirstMixedKindsAndRoles`, `selectionSurvivesRelistSortAndDropsRemovedKeys` |
| Mixed supported/unsupported types; only images classified Image | `directory_listing_tests` (types, links); `grid_ui_tests::mixedTilesFolderEntryAndParentNavigation` |
| Hidden excluded by default and flagged when included | `directory_listing_tests` (hidden, including the Windows attribute); `foldersFirstMixedKindsAndRoles` |
| Links listed with a badge, never listed through, never Image | `directory_listing_tests` (Windows junction verified); `mixedTilesFolderEntryAndParentNavigation` (badge visible, entering is refused with an inline notice) |
| Permission-denied, missing and file-as-folder are inaccessible, not empty | `directory_listing_tests` (icacls deny with guaranteed restore); `inaccessibleFolderReportsFailure`; `emptyInaccessibleAndNoFolderStates` |
| Cancellation (pre-cancelled and racing a 20,000-entry listing) carries no entries | `directory_listing_tests` |
| Truncation at a small limit stays sorted and flagged; exact limit is not truncation | `directory_listing_tests` |
| Newest generation wins (two rapid and 51 rapid `setDirectory` calls) | `directory_model_tests::newestGenerationWins` |
| A result arriving after deactivation is dropped | `staleResultAfterDeactivationIsDropped` |
| Selection by key survives relist, sort and refresh; removed keys drop; refresh never restores removed entries | `selectionSurvivesRelistSortAndDropsRemovedKeys`; `grid_ui_tests::externalRenameAndDeleteRefreshKeepSelection` (real watcher) |
| Same-folder refresh updates in place without a model reset | `incrementalRefreshInsertsAndRemovesInPlace`, `selectionSurvivesRelistSortAndDropsRemovedKeys` |
| Replace/Toggle/Range/ExtendRange/select-all/clear and count | `selectionModesAndCount`; `grid_ui_tests::multiSelectionKeyboardMouseAndCount` (mouse Ctrl/Shift/Ctrl+Shift, Shift+arrows/End, Ctrl+A, Ctrl+Space, Escape, the clear chip, header and status counts) |
| Grid keys beat remapped shortcuts (Ctrl+A bound to tree, Shift+Right bound to sort) | `multiSelectionKeyboardMouseAndCount`; existing `gridKeysBeatRemappedShortcutsAndCommandsAreScoped` |
| `thumbnailSource` empty for folders and non-images; epoch in the URL | `thumbnailSourceOnlyForImages`; UI: the provider never received a non-image path (`mixedTiles…`, `unsupportedActivationNeverDecodes`) |
| Unsupported Enter/double-click never decodes or opens | `unsupportedActivationNeverDecodes` (no `imageChanged`/`stateChanged`, notice names the file, Open disabled) |
| Enter/double-click on a folder navigates; tree and path bar follow; Backspace, Alt+Up and the Up button go to the parent with the cursor on the folder left | `mixedTilesFolderEntryAndParentNavigation` |
| Inactive model releases rows and does no work; hidden grid holds no listing or thumbnail requests | `inactiveByDefaultDoesNoWork`, `inactiveReleasesRowsAndKeepsSelection`; `virtualizedAndHidden` |
| 10,000+ entries stay bounded and responsive | `largeFolderStaysResponsive` (12,001 entries listed in 82 ms, worst GUI-thread gap 10 ms); `virtualizedAndHidden` and `auditTallGridQueueBound` with a generated 10,000-entry folder (fewer than 150 previews requested; at most 256 live delegates) |
| Existing grid behaviour (virtualization, remapping, modal blocking, sort, activation) | all earlier `grid_ui_tests` cases, now fed by generated folders through `fileGrid` |
| Empty, inaccessible, no-folder states; dark/light at 540×360 and 1180×780 | `emptyInaccessibleAndNoFolderStates`; `reviewCapturesMixedFolder` (opt-in) |
| Real application uses real thumbnails for grid tiles (one epoch) | Manual smoke run below |

## Evidence

Full suite on Windows 11, MSVC 2022, Qt 6.8.3, after merging `agent/panes` 52e10e0 (clean merge, no conflicts):

```
> ./scripts/build.ps1 -BuildDir build-file-grid -QtRoot C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64 -Test
 1/11 Test  #1: navigation .......................   Passed    2.25 sec
 2/11 Test  #2: directory_listing ................   Passed    7.16 sec
 3/11 Test  #3: viewer_acceptance ................   Passed    4.17 sec
 4/11 Test  #4: thumbnails .......................   Passed    5.24 sec
 5/11 Test  #5: folder_tree ......................   Passed    7.00 sec
 6/11 Test  #6: pane_layout ......................   Passed    0.23 sec
 7/11 Test  #7: window_slots .....................   Passed   19.56 sec
 8/11 Test  #8: folder_grid ......................   Passed   40.86 sec
 9/11 Test  #9: directory_model ..................   Passed    5.74 sec
10/11 Test #10: viewport .........................   Passed    0.05 sec
11/11 Test #11: themes ...........................   Passed    0.12 sec
100% tests passed, 0 tests failed out of 11
Total Test time (real) =  92.41 sec
```

Flake found and fixed before the merge. One earlier full run crashed in `mixedTilesFolderEntryAndParentNavigation`, inside `QQuickItem::height`. The cause was that the test helper `gridCell` returned a null delegate right after a relisting and the test used it unchecked. The helper now polls until the delegate exists, and every caller `QVERIFY`s the result. After the fix, the four new interaction cases passed 8 out of 8 repeated runs. A post-merge repeat loop of the whole `grid_ui_tests` executable was not run, because the session's permission policy refused it.

Linux core check: WSL Ubuntu 24.04, g++ 13.3, non-root, no CMake available there. Only the core sources and `directory_listing_tests` were compiled (`g++ -std=c++20 -Wall -Wextra -Wpedantic`), with the build output in /tmp:

```
ordering and types ok
empty/missing ok
hidden ok
directory symlink ok
file symlink ok
permission denied ok
cancellation ok (5/5 racing runs cancelled); 20000 entries listed in 157 ms
truncation ok
sort keys ok
directory listing tests passed
```

Core listing tests (`build-file-grid/directory_listing_tests.exe`):

```
ordering and types ok
empty/missing ok
hidden ok
windows junction ok
UNVERIFIED: directory symlink not creatable here (A required privilege is not held by the client.)
UNVERIFIED: file symlink not creatable here (A required privilege is not held by the client.)
permission denied ok
cancellation ok (5/5 racing runs cancelled); 20000 entries listed in 105 ms
truncation ok
sort keys ok
directory listing tests passed
```

Model tests: `directory_model_tests -o m.txt,txt` gives 16 passed, 0 failed (`listed 12001 entries in 82 ms; worst GUI gap 10 ms`). New UI cases run directly with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`: `grid_ui_tests mixedTilesFolderEntryAndParentNavigation unsupportedActivationNeverDecodes multiSelectionKeyboardMouseAndCount externalRenameAndDeleteRefreshKeepSelection emptyInaccessibleAndNoFolderStates reviewCapturesMixedFolder virtualizedAndHidden` gives 8 passed, 0 failed, 1 skipped (captures need the environment variable).

Review captures: with `GLANCELOT_REVIEW_CAPTURE=1`, files land in `build-file-grid/.cache/file-grid/`:
- `mixed-{dark,light}-{540,1180}.png`
- `notice-light-1180.png`
- `inaccessible-{dark,light}-540.png`
- `empty-light-540.png`

The implementer inspected them. As a result, the cursor focus ring and selection fill were confirmed distinct, the link badge and extension labels were confirmed legible in both modes, and the can't-open message was shortened to avoid a one-word last line.

Real-application smoke run with a scratch `GLANCELOT_CONFIG_DIR` and generated PNGs plus `notes.txt` and a subfolder:
- Command: `glancelot.exe --no-resume --show-grid --quit-after 5000 --screenshot … --window-size 1180x780 photo1.png`
- Exit code 0. The report showed `grid_visible: true`, `grid_live_cells: 7`, `thumbnail_cache_bytes: 1139200` and `thumbnail_pending: 0`.
- The screenshot shows real thumbnails, a folder tile and a TXT tile. This confirms the shared thumbnail epoch.

## Limits

- Windows directory and file symlinks could not be created without Developer Mode or elevation. Those cases are reported UNVERIFIED; the junction case ran.
- On Linux, the POSIX symlink, dangling-link and `chmod 000` cases passed in WSL. The Docker image later built and ran the Qt model, QML and UI tests (see Continuation fixes); physical Linux windowing remains unverified.
- On Windows, every reparse point counts as a link, as in `Navigator`. OneDrive Files-On-Demand placeholders may carry a cloud reparse tag. If they do, such photos appear as linked non-photo files. The filmstrip already excludes them, so this is not a regression, but the grid makes it visible. Classifying them as photos is a traversal-policy decision for the lead. One option is to badge only `IO_REPARSE_TAG_SYMLINK` and `IO_REPARSE_TAG_MOUNT_POINT`. Not verified on a OneDrive folder.
- Truncation at the real 100,000-entry limit is tested only in the core (small limit). The UI banner was not exercised with 100,001 generated files.
- Shuffle order in the grid uses `SortSpec`'s default seed. The controller's per-session shuffle seed is not reachable through the frozen interfaces, so with "Shuffle" the grid's file order differs from the filmstrip and canvas. Proposed lead change: expose the seed (for example `ViewerController::sortSeed()`, or a seed argument to `DirectoryEntryModel::setSort`).
- `ModifiedRole` converts `file_clock` ticks by offsetting against both clocks' `now()`. This works on both MSVC and libstdc++. The value is display-only and not yet shown in the UI.
- Network-share latency, a wedged share, and storage-aware concurrency (PERF-05) are bounded by design but not measured on SMB/HDD hardware: two workers, cancellation, a pool abandoned at teardown.
- Hidden-file display has no setting yet (UI-SETTINGS-01). Ctrl+arrow cursor-only movement is not provided.
- Live assistive technology, high-DPI and touch input are unverified.

## Lead decisions

- Shuffle seed: accepted. `ViewerController::sortSeed()` and a `seed` argument on `DirectoryEntryModel::setSort` (default 1) make the grid's shuffle order match traversal. Regression test: `directory_model_tests::shuffleSeedMatchesTraversalOrder`.
- Windows reparse points, including OneDrive Files-On-Demand cloud placeholders: keep the traversal policy for now. Every reparse point is listed as a linked non-photo file and is never decoded. Opening a cloud placeholder hydrates (downloads) it, and browsing must not trigger that silently. Treating placeholders as photos, with explicit download feedback, is a separate policy task that must be verified on a real OneDrive folder before it changes. Until then this is a known limitation; it is not a regression, since the filmstrip already excludes these files.

## Continuation fixes (`agent/continuation`, `5f01e4d`)

`agent/continuation` merges `agent/file-grid` (`bf7e6c6`) with `main` (`ef34492`), so it includes PANES-01. On top of that merge, `5f01e4d` adds the following changes:

- **Fail-closed listings.** A Windows `FindNextFileW` error other than `ERROR_NO_MORE_FILES`, or a POSIX `directory_iterator::increment` error, now makes the listing inaccessible. Before, the entries read so far were kept and published as if complete. The only early exit that may publish a partial result is still the intentional entry limit, which sets `truncated`. Cancellation is checked first and still returns `cancelled`.
- **Viewport kept across refresh.** A background model reset (an external rename or delete) now restores the item that was visible before the refresh. Before, it scrolled to the keyboard cursor even when the cursor was offscreen. If the cursor was visible before the reset, it stays in view. Regression test: `grid_ui_tests::resetRetainsScrolledViewport`. This test was first written by the independent reviewer against `bf7e6c6`, where it failed.
- **Compact grid.** Panes shorter than 420 px use a shorter header and smaller tiles (column pitch 136 instead of 176, minimum cell height 112), so more than one row shows.
- **Notices.** The notice timeout is now 4 s instead of 6 s. The truncation banner no longer says "the first" items, because the limit follows enumeration order, not display order.

Commands and actual output:

```text
./scripts/build.ps1 -BuildDir build-continuation -Test
100% tests passed out of 11
Total Test time (real) = 102.82 sec
docker build (Linux amd64, Qt 6.8.3, full CTest)
100% tests passed, 0 tests failed out of 11
Total Test time (real) =  55.47 sec
```

The Linux container now builds and passes the Qt model, QML and UI tests (`directory_model`, `folder_grid`). This replaces the earlier limit that only the core had been compiled on Linux. Under Docker the UI tests run on the offscreen platform; physical Linux windowing remains unverified.

## Integration

- Independent fix re-check of `b63e931..8db4014`: APPROVED. Only Low findings remain:
  - A grid viewport under 100 px still shows a half-clipped row. This predates the change.
  - There are no adopted tests for compact row snapping or for Try again. The reviewer's own versions of those tests passed.
  - The capture fix is a fixed `qWait(150)`.
  - `selectedCount` stays non-zero while the grid is inactive. This also predates the change.
- Integrated on `main` as `de63efb`:
  - `./scripts/build.ps1 -BuildDir build -Test` gave `100% tests passed out of 11`, `Total Test time (real) = 103.55 sec`.
  - `docker build -t glancelot-browser:main .` gave `100% tests passed, 0 tests failed out of 11`, `Total Test time (real) = 65.52 sec`.
