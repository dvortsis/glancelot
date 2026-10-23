# EN-04 Shared test harness wiring: handoff

Written for: the lead and the EN-04 reviewer. Session S2 (environment), Phase 0 cycle 1, October 22-23, 2026.

## Status: implemented and type-checked; build and UI suites not run

This session cannot install Qt 6.8.3: the cloud network policy denies `download.qt.io` (see docs/handoff/EN-02-HANDOFF.md on `claude/en-02-linux-bootstrap`). No targeted or full `ctest` ran. The changed files were type-checked against Ubuntu's Qt 6.4 headers (method and output below) and show exactly the same errors as unmodified `afeac1a`, all from Qt 6.5+ APIs the old headers lack. The acceptance "all UI suites pass unchanged" is **unverified**.

| Item | Value |
| --- | --- |
| Branch | `claude/en-04-harness-wiring` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`) |
| Commits | `996f067` test(EN-04) code; `148b2c0` cmake: provisional change for EN-04; then this handoff |
| Tip SHA | the commit that adds this handoff (`git log -1 claude/en-04-harness-wiring`) |

## What changed

- `src/app/viewer_context.hpp` (new, header-only, private app helper like `file_grid_wiring.hpp`): `struct ViewerContext` holds a reference to each of the ten services, and `installViewerContext(QQmlContext&, const ViewerContext&)` sets `viewer`, `paneLayout`, `monitorCustomization`, `filmstrip`, `folderTree`, `favoritesModel`, `folderHistory`, `fileGrid`, `theme`, `fileOperations`. References make every service required at compile time, so a harness cannot silently omit one.
- `app/main.cpp`: the ten `setContextProperty` lines become one `installViewerContext` call (designated initializers). Same names, same objects, same point before `loadFromModule`. No behaviour change in the app.
- `tests/ui_test_support.hpp` (used by `input_ui` and `pathbar_ui`), `tests/grid_ui_tests.cpp` (Harness wiring block only), `tests/fs_watch_ui_tests.cpp`, `tests/monitor_customization_ui_tests.cpp`: each harness owns a `FavoritesModel favorites` and `FolderHistory history`, declared before the engine so the engine is destroyed first, and calls `installViewerContext` instead of its own list.
- `CMakeLists.txt`, commit `cmake: provisional change for EN-04`: after `include(cmake/MonitorCustomization.cmake)`, a `BUILD_TESTING` loop adds `src/app/favorites_model.cpp` and `src/app/folder_history.cpp` (with headers, for AUTOMOC) to `input_ui_tests`, `pathbar_ui_tests`, `grid_ui_tests`, `fs_watch_ui_tests` and `monitor_customization_ui_tests`. This is unavoidable: those targets did not compile either class. S1 or the lead can fold the two sources into EN-05's `GLANCELOT_VIEWER_SOURCES` (or the UI-test source list) and drop the loop. `favorites_model.cpp` needs `settings_file.cpp`, which all five targets already compile; its quoted `settings_retry.hpp` include resolves beside the source.

One wiring site: after this branch, `grep -rn setContextProperty app src tests` matches only `src/app/viewer_context.hpp`.

## Behaviour in tests (for the reviewer)

The app is unchanged. In the five UI test executables `favoritesModel` and `folderHistory` are now defined, as in the app, so `ui/FolderTree.qml` takes its `typeof ... !== "undefined"` branches: the Favorites header and the Back/Forward buttons become visible in tests. That is the parity the packet asks for, but tree-geometry or focus-order assertions in `folder_grid`, `input_ui` and `pathbar_ui` are the places to watch. Isolation: `FavoritesModel` reads `settings.json` at construction; every harness sets `GLANCELOT_CONFIG_DIR` to a temporary directory before constructing it (`input_ui`/`pathbar_ui` `init()`, `grid_ui` `init()`, `monitor_customization_ui` `init()`, `fs_watch_ui` `initTestCase()`), and nothing writes favorites unless a test mutates them.

Kept out of scope: `main.cpp`'s FolderHistory and Favorites signal connections (`recordVisit`, `navigationRequested`, `setCurrentDirectory`) stay in `main.cpp`. In tests the history is installed but not fed, so Back/Forward stay disabled. Extracting those connections into a shared helper like `file_grid_wiring` would be a behaviour change for the UI suites; a candidate follow-up.

## Evidence (actual output, this session)

Type check of the six translation units that include the changed code, with `g++ 13.3 -std=c++20 -fsyntax-only` against Ubuntu 24.04's `qt6-base-dev`/`qt6-declarative-dev` 6.4.2 headers (installed only for this check), small stubs for the Qt 6.6+ headers `QQuickRhiItem` and `rhi/qrhi.h`, and empty `.moc` files. Run once on a detached `afeac1a` worktree and once on this branch; errors normalised by line number and compared:

```text
$ diff <(normalise syn-base.log | sort) <(normalise syn-new.log | sort) && echo "same errors as baseline"
same errors as baseline
$ grep -i -E "viewer_context|ViewerContext|favorites|history" syn-new.log
(no output)
```

Errors present in both (Qt 6.4 lacks these APIs):

```text
app/main.cpp:273:12: error: 'class QQmlApplicationEngine' has no member named 'loadFromModule'
app/main.cpp:335:78: error: 'CpuDevice' is not a member of 'QRhiDriverInfo'
src/app/native_title_bar.hpp:96:58: error: 'colorSchemeChanged' is not a member of 'QStyleHints'
src/render/output_color.hpp:28:39: error: no matching function for call to 'QImage::convertedToColorSpace(const QColorSpace&, QImage::Format ...
tests/grid_ui_tests.cpp:360:63 (and 415, 472, 591), tests/input_ui_tests.cpp:649:63: error: no matching function for call to 'QQmlApplicationEngine::singletonInstance<QObject*>(const char [10], ...
/usr/include/x86_64-linux-gnu/qt6/QtCore/qmetatype.h:842:23: error: invalid application of 'sizeof' to incomplete type 'QQuickCloseEvent'
```

The files checked: `app/main.cpp`, `tests/grid_ui_tests.cpp`, `tests/fs_watch_ui_tests.cpp`, `tests/monitor_customization_ui_tests.cpp`, `tests/input_ui_tests.cpp`, `tests/pathbar_ui_tests.cpp`; `src/app/viewer_context.hpp` alone compiled with no output.

## Unverified

- Configure, build and link with Qt 6.8.3, including the provisional CMake loop (AUTOMOC for the two added headers).
- Targeted `ctest -R "folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui"` and the full offscreen suite. The reviewer or the gate must supply this output; budget tests (`folder_grid`, `input_ui`) may need the documented single rerun.
- MSVC: designated initializers that bind reference members are standard C++20 and accepted by GCC 13; the Windows gate is the first MSVC compile of this pattern in the repository.
- The ui-design-skills do not apply: no QML or visual change was made.
