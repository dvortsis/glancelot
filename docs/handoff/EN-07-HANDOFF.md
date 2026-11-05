# EN-07 handoff: per-component command handling

Written for: the lead and the fresh EN-07 reviewer.

## Branch

- Branch: `claude/en-07`
- Exact base: `20f1210` (`origin/main`, Merge pull request #17). The brief named `7c0da34`; no commit had been made when PR 9 merged, so the branch fast-forwarded and the brief's "if it lands first" step applied. This is not a stacked packet.
- Code tip: `c785d1b`. The commit that adds this handoff and the queue row sits on top of it.
- Review with `git diff --color-moved=dimmed-zebra 20f1210 c785d1b -- ui`. The branch bodies are moved code with identifiers requalified.

## Summary

Each feature component now owns a `Connections { target: viewer }` handler for its own view-command ids. `handleViewCommand` went from 214 lines to 75 and keeps canvas, window and layout ids. It still calls `canvas.endLoupe()` first, for every id. `invoke` and scope routing did not change. No existing test or assertion was edited.

Nothing visible changes, so the ui-design-skills were not needed. No string, layout, icon or rendering code changed.

## Files changed

| File | Change |
| --- | --- |
| `ui/Main.qml` | Branches removed from `handleViewCommand`. Call sites gain `host` and `gridPane`/`treePane`. The `viewCommand` connection moved to the end of the file. `export-selection` moved to a `Connections` on the `exportFlow` Loader. |
| `ui/PhotoGrid.qml` | Existing end-of-file `Connections` extended, plus `property var host`. All in one block at the end of the file, clear of the PR 7 and PR 12 hunks. |
| `ui/FolderTree.qml` | Existing `Connections` extended. |
| `ui/DeleteFlow.qml`, `ui/RenameFlow.qml`, `ui/TransferHud.qml` | New `Connections`, plus `gridPane`/`treePane` properties (and `host` on TransferHud). |
| `tests/command_handlers_ui_tests.cpp` | New. Not built until the lead adds the cmake include below. |
| `docs/WORK-QUEUE.yaml` | The EN-07 row only. |
| `docs/handoff/EN-07-HANDOFF.md` | This file. |

## Commands moved

| Component | Commands | Note |
| --- | --- | --- |
| `PhotoGrid` | `view-details`, `grid-open`, `grid-up`, `grid-select-all`, `select-all`, `clear-selection`, `invert-selection`, `grid-zoom-in`, `grid-zoom-out`, `grid-zoom-reset`, `copy-path`, `copy-file`, `reveal-in-file-manager` | `filter` was already here |
| `FolderTree` | `tree-open-folder`, `focus-path`, `copy-folder-path`, `paste-path`, `open-terminal` | `add-to-favorites`, `navigate-back` and `navigate-forward` were already here |
| `DeleteFlow` | `delete-selection`, `delete-current-file`, `delete-current-folder` | |
| `RenameFlow` | `rename`, `new-folder` | |
| `TransferHud` | `cut-selection`, `copy-selection`, `paste` | |
| `exportFlow` Loader (in `Main.qml`) | `export-selection` | PR 9's handler; see the ordering trap below |

An extraction script compared the ids handled in `onViewCommand` and `handleViewCommand` across `ui/*.qml` before and after the move. The id set is identical, and no id appears in two handlers.

## Commands that stayed in Main

| Commands | Why |
| --- | --- |
| `monitor-*`, `workspace-*`, `pane-detach-*`, `pane-redock-*`, `pane-freeze-*` | They use Main's `commandTarget` and `pendingPaneFocus`, and they raise the window. `monitorSettings` also has to open from here. |
| `grid`, `view-preview`, `view-grid`, `view-combined`, `combined-orientation`, `frameless`, `fullscreen`, `focus-mode`, `arrange-panes`, `layout-lock`, `layout-reset`, `layout-preset-next` | Window and layout state, all owned by Main. |
| `open`, `palette`, `settings`, `shortcuts-help` | They open a window-level dialog. `ShortcutCheatSheet.qml` also calls `host.handleViewCommand("settings")` directly as a fallback. |
| `fit`, `actual`, `hold-actual`, `zoom-in`, `zoom-out`, `zoom-50`, `zoom-200`, `zoom-400`, `zoom-lock`, `rotate`, `background`, `navigator` | Canvas. |
| `copy-image` | It copies the decoded image on the canvas, with no pane target. |
| `reference-pin`, `reference-close` | Canvas-scope with no feature component of their own. |
| `slideshow`, `slideshow-slower`, `slideshow-faster` | The slideshow state and timers live in Main. |
| `quit` | Window. |

## Traps checked

- **Ordering.** QML connects a file's `Connections` in reverse declaration order. I measured this on Qt 6.8.3 with temporary logging, since removed. With Main's handler left where it was, every moved command would have run before `endLoupe`, and zoom lock reads the loupe state when an image changes. So Main's connection is now declared last, and it runs first:

  ```text
  ORDER Main.handle view-details
  ORDER Main view-details          (exportFlow Loader)
  ORDER TransferHud view-details
  ORDER RenameFlow view-details
  ORDER DeleteFlow view-details
  ORDER FolderTree view-details
  ORDER PhotoGrid view-details
  ORDER Main view-details          (slideshow block)
  ```

  The ids in the chain keep "end the loupe, then act". One order did change: `navigate-back`, `navigate-forward`, `add-to-favorites` and `filter` used to run before `endLoupe` and now run after it. None of them reads canvas state, and `endLoupe` touches only canvas zoom, pan, fit and loupe state, never focus. This order is a Qt implementation detail: no test asserts it, and a comment in `Main.qml` records why the block is last.
- **Components that aren't loaded.** PhotoGrid, FolderTree, DeleteFlow, RenameFlow and TransferHud are single, always-instantiated items, and a `Connections` still fires when its item is hidden. `ExportDialog.qml` is the exception: it doesn't exist until the first request. So `export-selection` is handled on the Loader, not in the dialog. It is the only command with this constraint.
- **Multiple instances.** `Main.qml` loads once per process. The only `createObject` is `DetachedPaneHost`, which reparents the existing panes and instantiates no feature component, so no handler runs twice.
- **Signal.** Same `viewCommand` signal, using `function onViewCommand` syntax. No property is named `onX`, and icon rendering is untouched.
- **Decoding and scans.** The handlers call the same controller and model methods as before. No new decoding or directory scan.
- **PR 7 and PR 12.** The `PhotoGrid.qml` change is one block at the end of the file, below all of their hunks.
- **Translations.** `qsTr` takes its context from the file name, so moving three delete notices out of `Main.qml` would have dropped them from the catalog's `Main` context and shown plain English in en_XA. They use `qsTranslate("Main", ...)`, so `translations/glancelot_en_XA.ts` is unchanged. `lupdate` is not installed on this PC, so the extraction itself is unverified; see "Unverified".

## Test results

These are QML-only changes, and the harnesses load `ui/*.qml` from the source tree. The full tree also rebuilt with the final QML, including `glancelot.exe` through qmlcachegen.

After the move, label `ui` without budget, on the final QML:

```text
 1/11 Test #14: pathbar_ui .......................   Passed   16.61 sec
 2/11 Test #26: fs_watch_ui ......................   Passed    9.92 sec
 3/11 Test #47: monitor_customization_ui .........   Passed   92.52 sec
 4/11 Test #48: translation_coverage_ui ..........   Passed   61.94 sec
 5/11 Test #49: layout_presets_ui ................   Passed   94.09 sec
 6/11 Test #50: command_routing ..................   Passed   27.01 sec
 7/11 Test #51: command_handlers_ui ..............   Passed  121.32 sec
 8/11 Test #52: reference_pin ....................   Passed   62.37 sec
 9/11 Test #53: reference_pin_native .............   Passed   51.74 sec
10/11 Test #54: export_ui ........................   Passed   61.42 sec
11/11 Test #55: stack_confirm_ui .................   Passed  225.08 sec
100% tests passed out of 11
Total Test time (real) = 827.00 sec
ctest exit: 0
```

After the move, other named suites:

```text
1/3 Test  #7: viewer_acceptance ................   Passed   19.05 sec
2/3 Test #29: shortcuts_help ...................   Passed    1.33 sec
3/3 Test #48: translation_coverage_ui ..........   Passed   52.02 sec
100% tests passed out of 3
Total Test time (real) =  72.68 sec
ctest exit: 0
```

After the move, budget suites under the gate lock:

```text
1/2 Test #12: folder_grid ......................   Passed  312.45 sec
2/2 Test #13: input_ui .........................   Passed   91.64 sec
100% tests passed out of 2
Total Test time (real) = 404.41 sec
ctest exit: 0
```

Final run after the last test edit:

```text
1/2 Test #50: command_routing ..................   Passed   15.10 sec
2/2 Test #51: command_handlers_ui ..............   Passed   94.98 sec
100% tests passed out of 2
Total Test time (real) = 110.11 sec
ctest exit: 0
```

`command_handlers_ui` is 28 passed, 0 failed (26 cases plus init and cleanup). It ran through a local, uncommitted cmake include, which is why it appears in the output above.

Before the move, at `7c0da34`, with no baseline failures:

```text
1/9 Test #14: pathbar_ui .......................   Passed    9.14 sec
2/9 Test #26: fs_watch_ui ......................   Passed    5.16 sec
3/9 Test #47: monitor_customization_ui .........   Passed   57.55 sec
4/9 Test #48: translation_coverage_ui ..........   Passed   62.89 sec
5/9 Test #49: layout_presets_ui ................   Passed  105.02 sec
6/9 Test #50: command_routing ..................   Passed   39.19 sec
7/9 Test #51: reference_pin ....................   Passed   69.17 sec
8/9 Test #52: reference_pin_native .............   Passed  100.77 sec
9/9 Test #53: stack_confirm_ui .................   Passed  337.84 sec
100% tests passed out of 9
Total Test time (real) = 788.98 sec
ctest exit: 0
```

```text
1/2 Test  #7: viewer_acceptance ................   Passed   18.71 sec
2/2 Test #29: shortcuts_help ...................   Passed    1.19 sec
100% tests passed out of 2
Total Test time (real) =  20.14 sec
ctest exit: 0
```

No baseline failures. No budget test needed a rerun. `input_ui` and `folder_grid` were not run before the move. `export_ui` did not exist at `7c0da34`, so it has no pre-move `ctest` run; its test below shows the handler is live.

`command_handlers_ui` also passed 28 of 28 on the unmoved QML, at both `7c0da34` and `20f1210`. That is the evidence for "no behaviour change": the same assertions pass before and after. One later test edit (below) made the guard case stricter. That edit ran against the moved QML only, not the unmoved QML.

## Fail-before evidence

The move changes no behaviour, so a new assertion can't fail against the base. Instead each moved branch was disabled in turn, by renaming its id or removing its guard, and the named test ran. 34 of 34 went red, and each file was restored and compared afterwards.

| Change | Suite | Test | Exit |
| --- | --- | --- | --- |
| PhotoGrid: view-details | `command_handlers_ui` | `gridViewCommandsStepOnce` | 1 (FAIL!) |
| PhotoGrid: view-details | `stack_confirm_ui` | `detailsToggleKeepsTheVisibleAnchor` | 1 (FAIL!) |
| PhotoGrid: grid-zoom-in | `command_handlers_ui` | `gridViewCommandsStepOnce` | 1 (FAIL!) |
| PhotoGrid: grid-zoom-out | `command_handlers_ui` | `gridViewCommandsStepOnce` | 1 (FAIL!) |
| PhotoGrid: grid-zoom-reset | `command_handlers_ui` | `gridViewCommandsStepOnce` | 1 (FAIL!) |
| PhotoGrid: select-all | `command_handlers_ui` | `gridSelectionCommandsApplyOnce` | 1 (FAIL!) |
| PhotoGrid: clear-selection | `command_handlers_ui` | `gridSelectionCommandsApplyOnce` | 1 (FAIL!) |
| PhotoGrid: invert-selection | `command_handlers_ui` | `gridSelectionCommandsApplyOnce` | 1 (FAIL!) |
| PhotoGrid: grid-select-all | `command_handlers_ui` | `gridSelectionCommandsApplyOnce` | 1 (FAIL!) |
| PhotoGrid: grid-open | `command_handlers_ui` | `gridOpenAndUpNavigate` | 1 (FAIL!) |
| PhotoGrid: grid-up | `command_handlers_ui` | `gridOpenAndUpNavigate` | 1 (FAIL!) |
| PhotoGrid: copy-path | `command_handlers_ui` | `copyPathAndFileUseTheGridSelection` | 1 (FAIL!) |
| PhotoGrid: copy-file | `command_handlers_ui` | `copyPathAndFileUseTheGridSelection` | 1 (FAIL!) |
| PhotoGrid: reveal-in-file-manager | `command_handlers_ui` | `revealAndTerminalReachTheController` | 1 (FAIL!) |
| FolderTree: open-terminal | `command_handlers_ui` | `revealAndTerminalReachTheController` | 1 (FAIL!) |
| FolderTree: tree-open-folder | `command_handlers_ui` | `treeOpenFolderOpensTheCursorRow` | 1 (FAIL!) |
| FolderTree: focus-path | `command_handlers_ui` | `focusPathShowsTheTreeAndFocusesTheAddress` | 1 (FAIL!) |
| FolderTree: copy-folder-path | `command_handlers_ui` | `copyFolderPathCopiesTheDirectory` | 1 (FAIL!) |
| FolderTree: paste-path | `command_handlers_ui` | `pastePathNavigatesToTheClipboardFolder` | 1 (FAIL!) |
| DeleteFlow: delete-selection | `command_handlers_ui` | `deleteCommandsExplainWhenNothingIsOpen` | 1 (FAIL!) |
| DeleteFlow: delete-current-file | `command_handlers_ui` | `deleteCommandsExplainWhenNothingIsOpen` | 1 (FAIL!) |
| DeleteFlow: delete-current-folder | `command_handlers_ui` | `deleteCommandsExplainWhenNothingIsOpen` | 1 (FAIL!) |
| RenameFlow: rename | `command_handlers_ui` | `renameTargetsTheOpenPhoto` | 1 (FAIL!) |
| RenameFlow: new-folder | `command_handlers_ui` | `newFolderTargetsTheCurrentDirectory` | 1 (FAIL!) |
| TransferHud: cut-selection | `command_handlers_ui` | `cutAndCopyMarkTheSelection` | 1 (FAIL!) |
| TransferHud: copy-selection | `command_handlers_ui` | `cutAndCopyMarkTheSelection` | 1 (FAIL!) |
| TransferHud: paste | `command_handlers_ui` | `pasteCopiesIntoTheCurrentFolder` | 1 (FAIL!) |
| Main: export-selection | `export_ui` | `emptySelectionExplainsAndOpensNothing` | 1 (FAIL!) |
| PhotoGrid: grid-zoom-in guard removed | `command_handlers_ui` | `gridCommandsKeepTheirPaneGuard` | 1 (FAIL!) |
| Main: endLoupe removed | `command_handlers_ui` | `aMovedCommandStillEndsTheLoupe` | 1 (FAIL!) |
| DeleteFlow: blocked reason removed | `command_handlers_ui` | `deleteSelectionShowsTheBlockedReason` | 1 (FAIL!) |
| RenameFlow: tree target removed | `command_handlers_ui` | `renameTargetsTheFocusedTreeFolder` | 1 (FAIL!) |
| TransferHud: tree target removed | `command_handlers_ui` | `pasteCopiesIntoTheFocusedTreeFolder` | 1 (FAIL!) |
| PhotoGrid: clear-selection guard removed | `command_handlers_ui` | `gridCommandsKeepTheirPaneGuard` | 1 (FAIL!) |

The first round showed `gridCommandsKeepTheirPaneGuard` could not fail when the `clear-selection` guard was removed: entering the grid selects the viewed photo, which hid a cleared one-item selection. The test now selects two items, and the row above is the re-run after that fix. The two existing-suite rows (`stack_confirm_ui`, `export_ui`) show existing tests also catch a removed handler.

## Grant check

`git diff --name-only 20f1210..<tip>`:

```text
docs/WORK-QUEUE.yaml
docs/handoff/EN-07-HANDOFF.md
tests/command_handlers_ui_tests.cpp
ui/DeleteFlow.qml
ui/FolderTree.qml
ui/Main.qml
ui/PhotoGrid.qml
ui/RenameFlow.qml
ui/TransferHud.qml
```

All are granted. No root CMake, shared header, C++ source or `AGENTS.md` change, and no new QML file.

## Failure and cancellation cases covered

The new test covers these failure and refusal paths:

- Nothing open: the delete notices, and the reveal and terminal status messages.
- The delete blocked reason on a drive root.
- The grid pane guards with the grid hidden.
- Delete requests stopping at the confirmation dialog and being cancelled, with the file still present.

## Decisions taken

1. **`copy-path`, `copy-file` and `reveal-in-file-manager` went to PhotoGrid; `open-terminal` went to FolderTree.** Their target resolution is almost all grid selection, or in the terminal's case the current folder. The alternative was keeping them in Main, which leaves file commands in a handler meant to shrink to canvas and window ids.
2. **`cut-selection`, `copy-selection` and `paste` went to TransferHud,** which already presents the copy-move service. The alternative, PhotoGrid, would grow a file PR 7 and PR 12 are editing.
3. **Panes are passed at the call site (`gridPane: photoGrid; treePane: treePanel`),** matching the existing `photoCanvas: canvas` pattern. The alternative was reaching Main's ids through the outer QML context, which is implicit and breaks under `pragma ComponentBehavior: Bound`.
4. **Moved notices use `qsTranslate("Main", ...)`** to keep the catalog unchanged. The alternative was regenerating the catalog into the `DeleteFlow` context, which needs `lupdate` (not installed here) and adds churn to a move.
5. **Main's connection is declared last,** so moved commands keep their order after `endLoupe`. The alternative was leaving it in place and arguing the order can't be observed, which would have flipped about 30 commands instead of 4.
6. **`export-selection` lives on the Loader.** A handler inside `ExportDialog.qml` would miss the request that first creates it.
7. **`host` is declared at the end of `PhotoGrid.qml`,** beside the block that uses it, to stay clear of PR 12's hunk near line 23.

## Open questions for the lead

1. **Add the test target.** Please add `cmake/CommandHandlersTests.cmake` as below and include it after `cmake/CommandRoutingTests.cmake`, inside `if(BUILD_TESTING)`, because it needs the monitor-customization helper. Until then the hosted job and the gate do not build or run this test.

   ```cmake
   # EN-07: each view command a feature component handles, through the shipped Main.qml. Sources and environment mirror command_routing.
qt_add_executable(command_handlers_ui_tests tests/command_handlers_ui_tests.cpp
  ${GLANCELOT_VIEWER_SOURCES}
  ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
  src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
  src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
  src/render/image_canvas.cpp src/render/image_canvas.hpp)
target_include_directories(command_handlers_ui_tests PRIVATE include src/render)
target_compile_definitions(command_handlers_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(command_handlers_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
qt_add_resources(command_handlers_ui_tests command_handlers_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
qt_add_resources(command_handlers_ui_tests command_handlers_brand_assets PREFIX "/" BASE assets FILES
  assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
  assets/brand/mascots/transparent/01_mascot_standing.png)
qt_add_shaders(command_handlers_ui_tests "command_handlers_canvas_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
glancelot_attach_monitor_customization(command_handlers_ui_tests)
add_dependencies(command_handlers_ui_tests glancelot-decoder)
add_test(NAME command_handlers_ui COMMAND command_handlers_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/command_handlers_ui-results.txt,txt" -o -,txt)
set_tests_properties(command_handlers_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 600)
glancelot_test_labels(command_handlers_ui LABELS ui process clipboard)
   ```

2. **Add the AGENTS.md rule.** Proposed wording for "Hot files":

   > New view commands are handled in the owning component: a `Connections { target: viewer; function onViewCommand(id) { ... } }` in that component's file, not a new branch in `ui/Main.qml`. `handleViewCommand` keeps canvas, window and layout ids, and ends the loupe for every id. A command that creates its component (a `Loader` item) goes on the Loader. A message moved out of `Main.qml` keeps its catalog context with `qsTranslate("Main", ...)` or is regenerated.

## Unverified

- Hosted `viewer` job and the full gate: not run at handoff. The tester gates the merge head.
- `lupdate` extraction of the `qsTranslate("Main", ...)` calls: `lupdate` and `lrelease` are not installed on this PC, so en_XA was not compiled and viewed. The catalog is unchanged, and the runtime lookup uses the same context and source text. A later catalog refresh should report no change for these three messages.
- Native and physical behaviour: all runs here were offscreen with software rendering, except `reference_pin_native` as that suite runs it. Nothing here is a physical qualification.
- Linux: not built locally. The PR's CI covers it.
