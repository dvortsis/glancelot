# EN-06 review: routing correctness (re-review after round 1)

- Reviewer: fresh-context Claude Code reviewer (Opus) on the lead's Windows PC. It did not implement EN-06 and did not do the stage-1 review.
- Branch reviewed: `origin/claude/en-06-routing` at `0b977db` (fix(EN-06): single navigate route).
- Diff reviewed: `git diff afeac1a origin/claude/en-06-routing` (whole packet), with emphasis on the round-1 commit `0b977db`.
- Date: 2026-10-22.

## Verdict

**Approved for integration.** The stage-1 blocking finding is fixed: `navigate-back` and `navigate-forward` now have exactly one executor (`FolderTree.qml`'s `onViewCommand`) on every route (invoke from the palette, menus and toolbar; Alt+Left/Alt+Right; canvas mouse Back/Forward; hidden or detached tree), and `canvas.endLoupe()` still runs for them. The new `folderHistoryStepsOnceOnEveryRoute` test fails when the removed `Main.qml` branches are restored and when the mouse route goes back to bypassing the emit. The targeted Windows suites pass (`folder_grid` on its one rerun after a timeout with no failed case). No blocking or medium findings. One low finding remains: the provisional CMake wiring (`21934df`, plus `folder_history.cpp` in `cmake/CommandRoutingTests.cmake`), which the lead re-lands at integration.

## Stage-1 review (summary; not committed as a document)

- Verified: `toggle-metadata` and `animation-*` run once (controller executes, `Main.qml` branches removed); dropping the `storage-profile-*` emit is justified (no view work, `io_scheduler` still passes); the merge leftovers at `Main.qml:1327` and `:2051` are split correctly; the QML-warning check in `mainQmlLoadsWithoutQmlWarnings` is real.
- Blocking: Alt+Left/Alt+Right and the palette/menu `navigate-back`/`navigate-forward` ran twice, because both `ui/Main.qml` `handleViewCommand` and `ui/FolderTree.qml` `onViewCommand` executed the emitted id.
- Low: provisional CMake commit `21934df` (the lead re-lands CMake at integration).

## Round-1 verification

### Every route reaches one navigation handler

All command routes end in one call site, `invoke()` (`ui/Main.qml:454`), which calls `viewer.trigger(id)`; `git grep viewer.trigger ui` finds only that line. Keys go `pressKey` -> `invoke`; palette, menus and the toolbar call `app.invoke`/`invokeTarget`. `ViewerController::trigger` has no branch for `navigate-back`/`navigate-forward`, so it reaches `else if(d->bindings.contains(id)) emit viewCommand(id)`. `d->bindings` is keyed by every registry id (`viewer_controller.cpp:267`; rebinding and rollback only replace values), so unbinding a key never stops the emit.

Two QML listeners receive `viewCommand`:

| Listener | Does for `navigate-*` |
| --- | --- |
| `Main.qml` `Connections` -> `handleViewCommand` | `canvas.endLoupe()` only (the branches are gone; comment at `:853`) |
| `FolderTree.qml:1025` `onViewCommand` | `folderHistory.navigateBack()` / `navigateForward()` (the single executor) |

`PhotoGrid.qml`'s `onViewCommand` handles only `filter`. After the change, `git grep -E 'navigate-back|navigate-forward|navigateBack|navigateForward' ui src` shows only the registry, the FolderTree handler, FolderTree's own back/forward buttons, the two canvas mouse lines (now `app.invoke`) and the cheat-sheet labels.

| Route | Path after round 1 | Executions |
| --- | --- | --- |
| `app.invoke` (palette, menus, toolbar) | invoke -> trigger -> emit -> FolderTree | 1 |
| Alt+Left / Alt+Right | Shortcut -> pressKey -> invoke -> ... -> FolderTree | 1 |
| Canvas mouse Back/Forward | `app.invoke("navigate-*")` (`Main.qml:1076-1077`) -> ... -> FolderTree | 1 (was 1 via the direct `handleViewCommand` call) |
| FolderTree header back/forward buttons | `folderHistory` directly (`FolderTree.qml:498,512`) | 1, unchanged |

Scope: both ids use the registry default scope (global), so `routeAllows` always passes and `commandBlockedReason` returns "" for them.

### Hidden and detached folder tree

- `FolderTree { id: treePanel }` (`Main.qml:1833`) is a static child, not a `Loader`. Hiding sets its size to 0 (`visible: width > 0 && height > 0`); a `Connections` object stays enabled on an invisible item.
- The tree is hidden under default settings (`viewer_controller.cpp:518-519`, `tree=false` when unset), and the harness does not show it, so `folderHistoryStepsOnceOnEveryRoute` exercises the hidden-tree case.
- Detached: the same object is reparented into the `DetachedPaneHost` (`Main.qml:354`). Both redock paths reparent it back to `app.contentItem` before the host is destroyed (`Main.qml:360-361`, `:369-370`). Closing the detached window does not destroy it either: `DetachedPaneHost.qml:39` sets `close.accepted=false` and invokes `pane-redock-<id>`. The detached case was checked by reading the code; `monitor_customization_ui` (`everyRealPaneMigratesAndReturnsWithoutDuplication`) migrates every real pane and back, and is in the Windows run below.

### Side effects preserved

- `canvas.endLoupe()` is still the first statement of `handleViewCommand` and still runs for `navigate-*` on every route, because the emit reaches `Main.qml`'s `Connections`. For the mouse route it previously ran through the direct call; it now runs through the emit.
- `toggle-metadata`: the removed QML branch called `wakeHud()`; `invoke()` calls `wakeHud()` after `viewer.trigger`, so explicit and key routes keep it.
- New, intended: the canvas mouse Back/Forward route now also calls `wakeHud()`, as the keys already did. This is the only user-visible change in the round-1 commit.
- `storage-profile-*` no longer end the loupe from an emit; they have no default key and are reached from the modal command search, which ends the loupe itself.
- Pre-existing and unchanged: FolderTree's own back/forward buttons call `folderHistory` directly and do not end the loupe.

### The new test would fail if either handler were restored

`folderHistoryStepsOnceOnEveryRoute` drives the shipped `Main.qml` (loaded from `GLANCELOT_TEST_SOURCE_DIR/ui/Main.qml`). Because that source path is compiled into the test binary, a scratch copy outside the worktree would not be loaded, so the two mutations below were made in place in `.worktrees\claude-en-06\ui\Main.qml` with `command_routing` run against the existing `build-claude-en-06` binaries (no rebuild needed: the QML is read from source). Each mutation was wrapped in `try { ... } finally { git checkout -- ui/Main.qml }`.

Mutation A: restore the two removed `handleViewCommand` branches.

```
-        // navigate-back/-forward run once in FolderTree's onViewCommand (FolderTree is always instantiated); here they only end the loupe.
+        else if (id === "navigate-back") { if (typeof folderHistory !== "undefined") folderHistory.navigateBack() }
+        else if (id === "navigate-forward") { if (typeof folderHistory !== "undefined") folderHistory.navigateForward() }
1/1 Test #44: command_routing ..................***Failed   17.21 sec
0% tests passed, 1 tests failed out of 1
FAIL!  : CommandRoutingTests::folderHistoryStepsOnceOnEveryRoute() 'problems.isEmpty()' returned FALSE. (invoke(navigate-back) requested navigation 2 times; Alt+Left requested navigation 2 times; mouse BackButton requested navigation 2 times; invoke(navigate-forward) requested navigation 2 times; Alt+Right requested navigation 2 times; mouse ForwardButton requested navigation 2 times)
Totals: 6 passed, 1 failed, 0 skipped, 0 blacklisted, 17070ms
```

All six routes, including the mouse buttons (now on `invoke`), double-run.

Mutation B: keep the branches removed but point the canvas mouse buttons back at `app.handleViewCommand` (the pre-round-1 mouse route).

```
-                if (mouse.button === Qt.BackButton) { app.invoke("navigate-back"); mouse.accepted = true; return }
-                if (mouse.button === Qt.ForwardButton) { app.invoke("navigate-forward"); mouse.accepted = true; return }
+                if (mouse.button === Qt.BackButton) { app.handleViewCommand("navigate-back"); mouse.accepted = true; return }
+                if (mouse.button === Qt.ForwardButton) { app.handleViewCommand("navigate-forward"); mouse.accepted = true; return }
1/1 Test #44: command_routing ..................***Failed   32.61 sec
0% tests passed, 1 tests failed out of 1
FAIL!  : CommandRoutingTests::folderHistoryStepsOnceOnEveryRoute() 'requests.size() >= 1' returned FALSE. (mouse BackButton)
Totals: 6 passed, 1 failed, 0 skipped, 0 blacklisted, 32450ms
```

The test also catches a mouse route that bypasses the emit (zero navigations), so the `app.invoke` rewiring is load-bearing.

After both mutations: `git -C .worktrees\claude-en-06 status --short` printed nothing and `rev-parse HEAD` printed `0b977db6cfc937a1fac6e57d823f7360d11c1f4a`. The unmutated rerun passed:

```
1/1 Test #44: command_routing ..................   Passed   14.36 sec
100% tests passed out of 1
PASS   : CommandRoutingTests::initTestCase()
PASS   : CommandRoutingTests::explicitRouteExecutesOnce()
PASS   : CommandRoutingTests::defaultKeysExecuteOnce()
PASS   : CommandRoutingTests::storageProfileIsControllerOnly()
PASS   : CommandRoutingTests::folderHistoryStepsOnceOnEveryRoute()
PASS   : CommandRoutingTests::mainQmlLoadsWithoutQmlWarnings()
PASS   : CommandRoutingTests::cleanupTestCase()
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 14252ms
```

The mutations were run outside the gate lock (targeted `ctest -R command_routing`, exempt; this suite has no timing budget).

### ui-design-skills lens

Read `application-interface-design` (precedence: platform behaviour and functional correctness; keyboard operation; prevent duplicate actions). The packet removes duplicate execution, which is exactly the "prevent duplicate submission" class of defect, and makes mouse Back/Forward follow the same command path and HUD feedback as the keys. No layout, spacing, typography, geometry, iconography, colour or motion changed; the two split lines are formatting only. No design-review pass was needed.

### Other findings

- Low (carried from stage 1): provisional CMake commit `21934df` and the `folder_history.cpp` source added to `cmake/CommandRoutingTests.cmake` in `0b977db`. The lead re-lands the CMake wiring at integration (add `command_routing_tests` to the monitor-customization consumer list, take `folder_history` from wherever EN-05 puts it).
- Info: the stale "temporary" CMake comments in the plan row are the lead's task, not this packet's.
- Info: `QWARN ... No QRhi found for window ..., QQuickRhiItem will not be functional` is printed in offscreen/software runs; it is not QML-originated, so `mainQmlLoadsWithoutQmlWarnings` correctly ignores it.

## Windows output

Build: the fixer's worktree `.worktrees\claude-en-06` at `0b977db` (clean, `git status --short` empty), incremental `& .\scripts\build.ps1 -BuildDir build-claude-en-06` (no `-Test`): `[53/53] Linking CXX executable glancelot.exe`, no errors. Windows 11, MSVC 2022, Qt 6.8.3 msvc2022_64, offscreen.

Run 1, inside `gate-lock.ps1 -Branch review/en-06 -Run { ... ctest --test-dir build-claude-en-06 -R '^(command_routing|input_ui|folder_grid|directory_model|animation|metadata|shortcuts_help|io_scheduler|monitor_customization_ui|folder_history)$' --output-on-failure }` (lock acquired 21:44:46 after `review/en-02`, `claude/f040-fix` and `claude/en-04-fix`):

```
 1/10 Test #12: folder_grid ......................***Timeout 400.00 sec
 2/10 Test #13: input_ui .........................   Passed  108.25 sec
 3/10 Test #16: directory_model ..................   Passed   22.41 sec
 4/10 Test #27: folder_history ...................   Passed    0.09 sec
 5/10 Test #28: shortcuts_help ...................   Passed    0.59 sec
 6/10 Test #31: io_scheduler .....................   Passed    0.23 sec
 7/10 Test #32: metadata .........................   Passed    0.14 sec
 8/10 Test #33: animation ........................   Passed    0.47 sec
 9/10 Test #43: monitor_customization_ui .........   Passed   24.17 sec
10/10 Test #44: command_routing ..................   Passed    7.61 sec

90% tests passed, 1 tests failed out of 10

Total Test time (real) = 564.55 sec

The following tests FAILED:
	 12 - folder_grid (Timeout)
```

`folder_grid` was killed by the 400 s ctest timeout with no failed case: `folder_grid-results.txt` holds 71 `PASS`, 0 `FAIL!`; the last completed case was `deleteCurrentFileAndFolderShortcuts` and it was inside `deleteSelectionKeyResolution` when killed. The fixer's run 1 timed out the same way (63 passed, no failure). A `docker` process was running on the machine during the run. EN-06 changes nothing the grid suite exercises beyond `Main.qml` routing, and every grid case that completed passed.

Rerun of `folder_grid` (one rerun for the budget suite), same lock wrapper, lock acquired after `lead/integrate-cycle1-a` (no `cl`, `ninja`, `link`, `docker` or other `ctest` process running at start):

```
Test project C:/src/Github/Glancelot/.worktrees/claude-en-06/build-claude-en-06
    Start 12: folder_grid
1/1 Test #12: folder_grid ......................   Passed  315.97 sec

100% tests passed out of 1

Total Test time (real) = 316.05 sec
```

`folder_grid-results.txt`: `Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 315730ms`.

Result: every suite in the targeted set passes on Windows, including the plan row's acceptance suites (`command_routing`, `input_ui`, `folder_grid`, `animation`, `metadata`). `folder_grid` needed its one rerun. Its runtime (316 s on a quiet machine, killed at 400 s under load in both this review and the fixer's run) is close to its timeout; that is the EN-03/EN-04 item already in the lead plan ("`folder_grid` at 228 s and growing"), not an EN-06 regression.

## Unverified

- Full offscreen suite and `docker build` (the integration gate decides).
- A physical mouse with Back/Forward buttons (the test synthesizes `Qt::BackButton`/`Qt::ForwardButton`).
- The detached-tree route at runtime (checked by code reading plus `monitor_customization_ui`).
