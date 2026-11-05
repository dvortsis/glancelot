# EN-07 review: per-component command handling

**Verdict: APPROVED**

- PR: 18, branch `claude/en-07`.
- Reviewed head: `d228f431a0686b5088662d58ee85696ae15fa769` (`d228f43`).
- Base: `20f12103cc6427ac0214f713476b1c1c88bbbbef` (`20f1210`, which is still `origin/main` and the merge base).
- Range: `git diff 20f1210 d228f43`.
- Reviewer: a fresh-context Opus subagent on the user's Windows PC. It did not implement EN-07 and wrote no earlier review.
- Checkout: a read-only detached worktree `.worktrees/review-en-07` at `d228f43`, built from a clean configure in `build-review-en-07` (Ninja, Release, `BUILD_TESTING=ON`, MSVC 2022, Qt 6.8.3 msvc2022_64). All 827 build steps passed.

## Findings

Nothing blocks. The move is mechanical: the same 80 ids are handled before and after, and the moved statements are identical once the requalification is undone. The loupe-order test fails when the connection goes back to its old place.

### Non-blocking

1. **The loupe order depends on when a connection is made, not on the Qt version.**
   - Main's handler runs first only because, in one synchronous creation of `Main.qml`, `QQmlObjectCreator::finalize` pops parser-status callbacks LIFO. So the last-declared `Connections` gets `componentComplete` (and connects) first, and QML notifier endpoints are called in connection order.
   - That holds for every `Connections` created with `Main.qml` itself, including those inside its child component files. It holds on qmlcachegen-compiled QML, which builds objects the same way.
   - It does not hold for a `viewCommand` handler created later. Examples are a `Loader` item, a `createObject` result such as `DetachedPaneHost`, a lazily created delegate, or a `Connections` whose `target` is rebound or that toggles `enabled`. Such a handler connects after Main's and would run before `endLoupe`.
   - Nothing in this PR does that. `export-selection` is handled on the Loader itself, which is created with `Main.qml`. `ExportDialog.qml` and `DetachedPaneHost` have no `viewCommand` handler. No moved `Connections` uses `enabled` or a changing `target`.
   - `loupeEndsBeforeAMovedHandlerActs` guards the six present owners (fail-before below), but it cannot guard a future handler in a dynamic object.
   - Recommendation for the lead's AGENTS.md wording: say that a `viewCommand` handler must live in an object created with `Main.qml`, not in a Loader item or a dynamically created object. The handoff's draft already covers the Loader case; add the general rule and the reason (it would run before the loupe ends).
2. **Nit: the new handlers dereference `host`, `gridPane` and `treePane` without a guard.** For example, `root.gridPane.deletionTargets()` in `TransferHud.qml` and `panel.host.paneAvailable(...)` in `PhotoGrid.qml`.
   - This is safe today, because `Main.qml` is the only file that instantiates these components with those properties set.
   - `translation_coverage_ui` creates `TransferHud.qml` on its own with only `service` set, but it never emits `viewCommand`. Its run below passes.
   - A future standalone harness that fires `cut-selection`, `copy-selection` or `paste` would get a TypeError rather than silence. It needs no change for this packet.

### Info

3. **Four ids now run after `endLoupe` instead of before it: `navigate-back`, `navigate-forward`, `add-to-favorites` and `filter`. No visible behaviour changes.**
   - `navigate-back` and `navigate-forward` go through `folderHistory` to `ViewerController::navigateDirectory`. That function emits `explicit-navigation`, sets `directoryPending`, emits `stateChanged` and submits the listing to the navigation worker. The image changes later, asynchronously, so `endLoupe` has long finished in either order.
   - `add-to-favorites` writes the favourites model. `filter` opens the grid's filter field. Neither reads canvas state.
   - `ImageCanvas::endLoupe` restores zoom, pan and fit, unwatches a hold key and emits `viewChanged`, `loupeChanged` and `update()`. It touches no focus.
   - No QML handles `loupeChanged`. `onViewChanged` only queues a viewport sync and recomputes the navigator rectangle.
   - The new order is "end the loupe, then act", which every Main-handled id already followed.
4. **The handoff's "lupdate unverified" line is now verified** (see "Translations").

## 1. No behaviour change

### Same id set

I wrote my own extractor rather than use the handoff's table. It brace-matches every `function onViewCommand(x)` and `function handleViewCommand(x)` in `ui/*.qml` and collects the `x === "..."` and `x.indexOf("...")` literals. The output:

```text
=== 20f1210
ui/FolderTree.qml:onViewCommand@1038: 3 ids: add-to-favorites navigate-back navigate-forward
ui/Main.qml:handleViewCommand@751: 75 ids: actual arrange-panes background clear-selection combined-orientation copy-file copy-folder-path copy-image copy-path copy-selection cut-selection delete-current-file delete-current-folder delete-selection export-selection fit focus-mode focus-path frameless fullscreen grid grid-open grid-select-all grid-up grid-zoom-in grid-zoom-out grid-zoom-reset hold-actual invert-selection layout-lock layout-preset-next layout-reset monitor-disable monitor-enable monitor-identify monitor-move-output monitor-recover monitor-settings monitor-swap navigator new-folder open open-terminal palette pane-detach-* pane-freeze-* pane-redock-* paste paste-path quit reference-close reference-pin rename reveal-in-file-manager rotate select-all settings shortcuts-help slideshow slideshow-faster slideshow-slower tree-open-folder view-combined view-details view-grid view-preview workspace-apply workspace-reset workspace-save zoom-200 zoom-400 zoom-50 zoom-in zoom-lock zoom-out
ui/Main.qml:onViewCommand@713: 1 ids: explicit-navigation
ui/PhotoGrid.qml:onViewCommand@1083: 1 ids: filter
total distinct: 80 dups: {}
=== d228f43
ui/DeleteFlow.qml:onViewCommand@118: 3 ids: delete-current-file delete-current-folder delete-selection
ui/FolderTree.qml:onViewCommand@1038: 8 ids: add-to-favorites copy-folder-path focus-path navigate-back navigate-forward open-terminal paste-path tree-open-folder
ui/Main.qml:handleViewCommand@751: 48 ids: actual arrange-panes background combined-orientation copy-image fit focus-mode frameless fullscreen grid hold-actual layout-lock layout-preset-next layout-reset monitor-disable monitor-enable monitor-identify monitor-move-output monitor-recover monitor-settings monitor-swap navigator open palette pane-detach-* pane-freeze-* pane-redock-* quit reference-close reference-pin rotate settings shortcuts-help slideshow slideshow-faster slideshow-slower view-combined view-grid view-preview workspace-apply workspace-reset workspace-save zoom-200 zoom-400 zoom-50 zoom-in zoom-lock zoom-out
ui/Main.qml:onViewCommand@2201: 1 ids: export-selection
ui/Main.qml:onViewCommand@713: 1 ids: explicit-navigation
ui/PhotoGrid.qml:onViewCommand@1085: 14 ids: clear-selection copy-file copy-path filter grid-open grid-select-all grid-up grid-zoom-in grid-zoom-out grid-zoom-reset invert-selection reveal-in-file-manager select-all view-details
ui/RenameFlow.qml:onViewCommand@34: 2 ids: new-folder rename
ui/TransferHud.qml:onViewCommand@18: 3 ids: copy-selection cut-selection paste
total distinct: 80 dups: {}
```

`Compare-Object` of the two sorted id lists printed nothing. `handleViewCommand` went from 75 ids to 48, so 27 moved: PhotoGrid 13, FolderTree 5, DeleteFlow 3, RenameFlow 2, TransferHud 3 and the export Loader 1. No id is handled twice.

### Same statements

A second script took the 26 base branches of the 27 moved ids (`copy-path` and `copy-file` share one branch) and the new handler bodies. It undid the requalification mechanically:

- `(flow|root|panel).host.` becomes nothing.
- `*.gridPane` and PhotoGrid's `panel` become `photoGrid`.
- `*.treePane` and FolderTree's `panel` become `treePanel`.
- `flow.` becomes `deleteFlow.`, `root.open*` becomes `renameFlow.open*`, and `qsTranslate("Main", ` becomes `qsTr(`.

It then compared the two as multisets of statements:

```text
old moved branches: 26
old statements: 119 new statements: 120
only in old: {'if (id === "tree-open-folder") treePanel.openCursor()': 1}
only in new: {'if (id === "tree-open-folder")': 1, 'treePanel.openCursor()': 1}
ui/PhotoGrid.qml base body is a prefix of head body: True
ui/FolderTree.qml base body is a prefix of head body: True
```

The only difference is layout: a one-line branch became a block. The pre-existing `filter`, `add-to-favorites` and `navigate-*` branches are untouched.

The requalified names resolve to the same objects and functions:

- `workspace`, `gridScope`, `gridMode`, `paneAvailable`, `focusWithin` and `commandBlockedReason` are all members of `app` (`Main.qml` lines 178, 484, 32, 317, 469 and 503), so `host.X` is the same lookup.
- `panel` is the root id of both `PhotoGrid.qml` and `FolderTree.qml`.
- `fileGrid` and `viewer` are root-context properties (`src/app/viewer_context.hpp`). No component file declares a property or id that shadows them, `host`, `gridPane` or `treePane`.
- PhotoGrid had no earlier unqualified use of `host`, so the new `property var host` shadows nothing.

### Guards, returns and "accepted"

- Every pane guard is kept verbatim, for example `if (paneAvailable("grid") || gridScope)` on `select-all`, `clear-selection`, `invert-selection` and the three `grid-zoom-*` ids.
- The early `return` after the blocked reason in `delete-selection` now leaves DeleteFlow's handler, not `handleViewCommand`. That is equivalent, because nothing followed that branch in the old `else if` chain.
- "Accepted" does not apply. `handleViewCommand` returns nothing. Its only direct caller outside the signal is `ShortcutCheatSheet.qml:401` (`host.handleViewCommand("settings")`), and `settings` stayed in Main (`git grep -n handleViewCommand d228f43 -- ui src tests app`).
- `ViewerController::trigger` checks `receivers(SIGNAL(viewCommand(QString))) == 0` for `copy-path`, `copy-file` and `copy-image`. The receiver count only grows, so that fallback is unchanged.
- A handler that emits a nested `viewCommand`, such as `grid-open`, `grid-up` or `tree-open-folder` through `navigateDirectory`'s `explicit-navigation`, nests exactly as before. Only the slideshow block handles `explicit-navigation`.

### Components that are hidden, unloaded or repeated

- `PhotoGrid`, `FolderTree`, `DeleteFlow`, `RenameFlow` and `TransferHud` are each instantiated once, as direct children of the `ApplicationWindow` in `Main.qml`. None of them is in a Loader, Repeater or Instantiator.
- A `Connections` fires whether its item is visible or not. The old code ran these branches regardless of visibility too, with the same pane guards.
- The export handler sits on the `exportFlow` Loader, which exists from startup, so the first `export-selection` still creates the dialog.
- `app/main.cpp:273` calls `engine.loadFromModule("Glancelot","Main")` once, and `Main.qml` is the only QML file that instantiates these components. `DetachedPaneHost` (`createObject`) reparents the existing panes, so no handler exists twice.
- The only other instance is the standalone `TransferHud.qml` in `translation_coverage_ui`. Its engine sets `viewer`, so `target: viewer` resolves, and it never emits `viewCommand` (Nit 2).

## 2. Loupe order

- Main's `viewCommand` connection is now the last object declared in `Main.qml` (line 2244). Its run-first behaviour holds for every handler created with `Main.qml` (Finding 1).
- `loupeEndsBeforeAMovedHandlerActs` reads `canvas.loupeActive` from a C++ slot on the first signal each owner's handler emits synchronously.

Fail-before: a scratch script (outside the repository) put `function onViewCommand(id) { app.handleViewCommand(id) }` back into the `Connections` that holds `onTreeShown` (its old place) and removed the trailing block:

```text
@@ -830,6 +830,7 @@ ApplicationWindow {
     Connections {
         target: viewer
+        function onViewCommand(id) { app.handleViewCommand(id) }
         function onTreeShown() { app.hudVisible = true; hideHud.restart(); Qt.callLater(treePanel.focusList) }
     }
@@ -2239,10 +2240,4 @@ ApplicationWindow {
-    // Declared last on purpose: ...
-    Connections {
-        target: viewer
-        function onViewCommand(id) { app.handleViewCommand(id) }
-    }
```

The harness loads `ui/Main.qml` from the source tree, so no rebuild was needed. `command_handlers_ui_tests.exe loupeEndsBeforeAMovedHandlerActs` with the mutation:

```text
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(PhotoGrid) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(FolderTree) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(TransferHud) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(DeleteFlow) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(RenameFlow) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
FAIL!  : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(exportFlow) '!probe.loupeAtFirstHit' returned FALSE. (the handler acted before Main.qml ended the loupe)
Totals: 2 passed, 6 failed, 0 skipped, 0 blacklisted, 14922ms
exe exit: 6
```

All six rows fail, so none of them passes vacuously: each watched signal comes from the owner's handler, not from something else. After `git checkout -- ui/Main.qml`, `git status --porcelain` printed nothing, and the same command gave:

```text
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(PhotoGrid)
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(FolderTree)
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(TransferHud)
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(DeleteFlow)
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(RenameFlow)
PASS   : CommandHandlersUiTests::loupeEndsBeforeAMovedHandlerActs(exportFlow)
Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 11578ms
exe exit: 0
```

The rows cover all six owners. A future handler in another component, or one that connects late, is not covered (Finding 1).

## 3. Translations

`lupdate` 6.8.3 (Linux, WSL, `~/glancelot-aqt/qt/6.8.3/gcc_64/bin/lupdate`) ran on trees exported with `git archive`, using `lupdate ui src app -locations none -no-obsolete -ts translations/glancelot_en_XA.ts`. Each result was compared with the committed catalog using `diff --strip-trailing-cr`:

```text
=== head: lupdate
Updating 'translations/glancelot_en_XA.ts'...
    Found 657 source text(s) (0 new and 657 already existing)
--- head: diff against the committed catalog
IDENTICAL (head)
=== base: lupdate
    Found 657 source text(s) (0 new and 657 already existing)
--- base: diff against the committed catalog
IDENTICAL (base)
```

The three notices stay in the `Main` context. The nearest `<name>` above catalog line 795 is `Main`, and they keep their en_XA translations.

As a control, switching DeleteFlow's three calls back to `qsTr(` makes `lupdate` move them into a new `DeleteFlow` context as `type="unfinished"` and drop them from `Main`:

```text
143a144,155
>     <message>
>         <source>Select items in the grid to move them to the %1.</source>
>         <translation type="unfinished">[Séléçt îtéms îñ thé grîd tõ mõvé thém tõ thé %1.]</translation>
...
793,804d804
<         <source>Select items in the grid to move them to the %1.</source>
...
```

So `qsTranslate("Main", ...)` is what keeps the catalog unchanged, and `translations/glancelot_en_XA.ts` needs no regeneration.

## 4. The new test target

- `cmake/CommandHandlersTests.cmake` mirrors `cmake/ExportUiTests.cmake`. Its sources are the test, `selection_export_service` (needed because `export-selection` loads `ExportDialog.qml`), and the same viewer, context and pane sources every Main.qml harness uses. Nothing extra.
- The root `CMakeLists.txt` change is the single `include(cmake/CommandHandlersTests.cmake)` line inside `if(BUILD_TESTING)`, after `CommandRoutingTests.cmake`. It follows `MonitorCustomization.cmake`, which defines `glancelot_attach_monitor_customization`.
- It configured and built from a clean build directory. In the generated `CTestTestfile.cmake`:

  ```text
  set_tests_properties("command_handlers_ui" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "ui;process;clipboard" RESOURCE_LOCK "clipboard" TIMEOUT "600" ...
  ```

- The labels are right by the definitions in `cmake/TestLabels.cmake`:
  - `ui`: it loads `ui/Main.qml`.
  - `process`: `ViewerController::open` starts `glancelot-decoder`.
  - `clipboard`: `copy-path`, `copy-file`, `copy-folder-path` and `paste-path` use the system clipboard. The label adds `RESOURCE_LOCK clipboard`.
  - Not `budget`: it has no wall-clock assertion.
  - Not `native`: `reveal-in-file-manager` and `open-terminal` run only with nothing open, deletes stop at the confirmation, and `paste` copies into a temporary folder.
- `tests/command_handlers_ui_tests.cpp` has no `qWait`, sleep or plain `QTRY_*`.
  - Every `QTRY_*` is `_WITH_TIMEOUT(..., 10000)`.
  - Every `QTest::qWaitFor` has a deadline of 10000 or 15000.
  - Cleanup fails on any QML warning.
  - Test media are generated in `QTemporaryDir`, and nothing is confirmed for deletion.

## 5. Grant

```text
$ git diff --name-only 20f1210 d228f43
CMakeLists.txt
cmake/CommandHandlersTests.cmake
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

- `CMakeLists.txt` has one added line.
- `docs/WORK-QUEUE.yaml` changes only the EN-07 row: `state: review` and `evidence:`.
- No shared header, C++ source, `AGENTS.md` or new QML file is touched.

```text
$ python scripts/ci/pr_guard.py --base origin/main --head d228f43 --head-ref claude/en-07
pr_guard: code=True, 0 error(s)
```

## 6. Test runs (Windows, this PC, offscreen)

All runs used the clean build at `d228f43`. Before them, `git status --porcelain` was empty and `HEAD` was `d228f431a0686b5088662d58ee85696ae15fa769`. Targeted runs used `scripts/build-slot.ps1`; the budget suites used `scripts/gate-lock.ps1`.

`ctest -R "^(viewer_acceptance|shortcuts_help)$"`:

```text
1/2 Test  #7: viewer_acceptance ................   Passed   11.28 sec
2/2 Test #29: shortcuts_help ...................   Passed    1.06 sec
100% tests passed out of 2
Total Test time (real) =  12.46 sec
ctest exit: 0
```

`ctest -L ui -LE budget`, which includes `command_handlers_ui`, `command_routing`, `translation_coverage_ui`, `export_ui` and `stack_confirm_ui`:

```text
 1/11 Test #14: pathbar_ui .......................   Passed    5.61 sec
 2/11 Test #26: fs_watch_ui ......................   Passed    4.04 sec
 3/11 Test #47: monitor_customization_ui .........   Passed   37.50 sec
 4/11 Test #48: translation_coverage_ui ..........   Passed   47.39 sec
 5/11 Test #49: layout_presets_ui ................   Passed   69.39 sec
 6/11 Test #50: command_routing ..................   Passed   13.64 sec
 7/11 Test #51: command_handlers_ui ..............   Passed   71.05 sec
 8/11 Test #52: reference_pin ....................   Passed   34.30 sec
 9/11 Test #53: reference_pin_native .............   Passed   51.13 sec
10/11 Test #54: export_ui ........................   Passed   41.84 sec
11/11 Test #55: stack_confirm_ui .................   Passed  208.83 sec
100% tests passed out of 11
Total Test time (real) = 585.74 sec
ctest exit: 0
```

`ctest -R "^(folder_grid|input_ui)$"` under the gate lock:

```text
1/2 Test #12: folder_grid ......................   Passed  272.40 sec
2/2 Test #13: input_ui .........................   Passed   95.64 sec
100% tests passed out of 2
Total Test time (real) = 368.22 sec
ctest exit: 0
```

Both passed on the first run, so no rerun was needed. The gate lock was queued behind the lead's PR 19 gate and ran after it.

No existing test or assertion changed: the diff touches no file under `tests/` except the new `command_handlers_ui_tests.cpp`.

## Not verified

- The full gate (`scripts/build.ps1 -Test` plus `docker build`). The tester gates the merge head.
- The full suite, except as the hosted CI run it. Run `42090324386` on head `d228f431` finished with `success`: `guard`, `core (ubuntu-24.04)`, `core (windows-2022)`, `viewer` (44m7s) and `ci-ok` all passed.
- Linux. I built and ran nothing on Linux; WSL was used only for `lupdate`.
- en_XA was not compiled with `lrelease` and viewed. The catalog is unchanged and the runtime context is the same.
- Native rendering and physical behaviour. Every run here is offscreen with software rendering, except `reference_pin_native` as that suite runs itself. Nothing here is a physical qualification.
- The ui-design-skills were not needed: no string, layout, icon, motion or rendering change.
