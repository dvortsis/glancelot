# Test the current application

On Windows, double-click **Test Glancelot.cmd** in the checkout root. It builds this checkout, runs the full automated suite, packages the viewer and decoder, and opens the native app. The first build takes longer; later builds are incremental. Visual Studio 2022 C++ Build Tools and the local Qt/tools setup in [BUILDING.md](../BUILDING.md) are required. No dependencies are downloaded by the launcher.

After that, **Open Last Test Build.cmd** opens the last successful test package immediately. It does not rebuild or include later source changes. Use **Test Glancelot.cmd** after pulling or changing code. Close the test app before rebuilding. Build/test failures stop the launch and stay visible in the console.

The console stays open so you can read the result; pressing a key closes the launcher window while the app keeps running. It shows the branch, full commit, whether there were local changes, and the time tests passed. This record is also in `dist/Glancelot/test-build.json`. The fast launcher checks the viewer and decoder hashes so an overwritten package cannot silently impersonate the tested build. These checks identify local builds, not signed releases.

Use Ctrl+O or drop a JPEG/PNG into the window. Ctrl+, opens Settings; Ctrl+Shift+P opens the command palette. Test settings and window resume state are kept separately in `.cache/manual-testing/config` and reused across launches, leaving normal application preferences alone. Photos opened for viewing are not changed. The launcher does not reset settings or copy your photo library.

PowerShell equivalents (run from the checkout root):

```powershell
./scripts/test-app.ps1
./scripts/test-app.ps1 -NoBuild
./scripts/test-app.ps1 -Path 'C:\Photos\Example image.png'
./scripts/test-app.ps1 -NoLaunch  # build, test and package without opening a window
```

Each checkout/worktree builds and packages its own current branch under `build-manual-testing` and `dist/Glancelot`. The root shortcuts test the integrated checkout; experimental branches require running their own launcher from their assigned worktree. To test default preferences, close this checkout's test windows and rename `.cache/manual-testing/config` before reopening.

For portable sharing, extract `dist/Glancelot.zip` and run `bin/glancelot.exe`; keep all bundled files together. The test-build record is local and is created after packaging, so it is not included in this ZIP. For the Linux browser desktop, use the [Docker instructions](DOCKER.md); this launcher does not rebuild or update a running Docker deployment.

Useful manual checks: open a generated/disposable photo folder, navigate with arrows, toggle folders (Ctrl+B) and the filmstrip (T), exercise grid/preview and pane layouts, switch themes, then close and reopen to check window resume. Hardware-dependent display/color behavior still requires native testing on the target hardware.

## Run part of the automated suite

Every CTest test carries one or more labels.
- Tests declare `ui`, `budget`, `native`, `process` and `clipboard` with `glancelot_test_labels()` from `cmake/TestLabels.cmake`.
- Configure computes `fast` at the end of the root `CMakeLists.txt`, so a test with no declaration is `fast`.
- Configure fails only if a declaration names a test that is not registered, or uses an unknown label or lock.

Run these commands from a configured build directory; on Linux, set `QT_QPA_PLATFORM=offscreen` first.

| Label | Meaning | Tests |
| --- | --- | --- |
| `fast` | The quick loop, computed: every test without `ui`, `budget` or `native` | 27 tests |
| `ui` | Loads the shipped QML (`ui/Main.qml`) in a `QQuickWindow` | `folder_grid`, `input_ui`, `pathbar_ui`, `fs_watch_ui`, `monitor_customization_ui` |
| `budget` | Asserts wall-clock budgets and can fail under machine load | `fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui` |
| `native` | Touches the real OS: desktop shell integration, the trash, `subst`, `cmd` `mklink`, or a window on the real platform plugin (not forced offscreen) | `platform_integration`, `file_operation_service`, `recovery_process`, `folder_grid`, `window_slots`, `fs_watch`, `file_operations`, `directory_listing`, `selection_export` |
| `process` | Starts any child process, directly or through product code: `glancelot-decoder` (through `ViewerController::open` or `ThumbnailService`), a test helper, the app, Python, `cmd` or `subst`. Does not remove a test from `fast` | 22 tests: `selection_export`, `directory_listing`, `file_operations`, `viewer_acceptance`, `thumbnails`, `folder_tree`, `window_slots`, `folder_grid`, `input_ui`, `file_operation_service`, `platform_integration`, `fs_watch`, `fs_watch_budget`, `filter`, `cache`, `io_scheduler`, `copy_move`, `recovery`, `recovery_startup`, `recovery_controller`, `recovery_process`, `monitor_customization_ui` |
| `clipboard` | Reaches the system clipboard, directly or through product code | `folder_grid`, `input_ui`, `metadata` |

Common commands:

```sh
ctest -L fast --output-on-failure        # quick loop while you work (118 s serial measured on the Windows gate PC)
ctest -LE budget --output-on-failure     # everything except the timing-budget tests (36 tests)
ctest -L ui --output-on-failure          # the real-QML tests only
ctest -N -L budget                       # list the tests a label selects, without running them
```

`-L` and `-LE` take a regular expression; use `-L '^ui$'` if a future label could contain `ui`.

- **Tests with labels can run in parallel.** Budget tests hold `RESOURCE_LOCK budget` and clipboard tests hold `RESOURCE_LOCK clipboard`, so `ctest -j N` never runs two of either kind at once. Budget tests still compete with everything else running on the machine, so run them serially, or with nothing heavy alongside, when timing matters.
- **These subsets do not replace the full suite.** Before a handoff, run the full suite (`ctest` with no label), then `scripts/build.ps1 -Test` on Windows. The authoritative gate always runs everything.
- **When a packet adds a test,** label it in the packet's own `cmake/<Feature>Tests.cmake`, right after its `add_test`. Do not edit `cmake/TestLabels.cmake`.

  ```cmake
  add_test(NAME reference_pin COMMAND reference_pin_tests ...)
  glancelot_test_labels(reference_pin LABELS process clipboard)
  ```

  Add each label that applies:
  - `ui` if it loads `ui/Main.qml`;
  - `budget` if it asserts timing. This also adds `RESOURCE_LOCK budget`;
  - `native` if it touches the real shell, trash, `subst`, `mklink` or a window that is not forced offscreen;
  - `process` if it starts any child process, including the decoder through `ViewerController::open` or `ThumbnailService`;
  - `clipboard` if it reaches the system clipboard, including through product code such as the viewer's or metadata model's copy functions. This also adds `RESOURCE_LOCK clipboard`.

  `LOCKS budget clipboard` adds a lock without the label. Never declare `fast`: a test with none of `ui`, `budget` or `native` gets it automatically, and that includes a test with no `glancelot_test_labels` call at all.
