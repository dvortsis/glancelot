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

Every CTest test carries one or more labels. They are defined in `cmake/TestLabels.cmake`, and configure fails if a test has none. Run these commands from a configured build directory; on Linux, set `QT_QPA_PLATFORM=offscreen` first.

| Label | Meaning | Tests |
| --- | --- | --- |
| `fast` | The quick loop: every test that is not `ui` or `budget`, except `recovery_process` | 32 tests |
| `ui` | Loads the shipped QML (`ui/Main.qml`) in a real window | `folder_grid`, `input_ui`, `pathbar_ui`, `fs_watch_ui`, `monitor_customization_ui` |
| `budget` | Asserts wall-clock budgets and can fail under machine load | `fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui` |
| `process` | The test itself starts a child process (decoder, helper, the app or Python) | `thumbnails`, `io_scheduler`, `fs_watch`, `window_slots`, `recovery`, `recovery_startup`, `platform_integration`, `recovery_process` |
| `native` | Depends on the platform shell, trash, screens or a real app window | `platform_integration`, `file_operation_service`, `monitor_output`, `monitor_customization`, `monitor_customization_ui`, `recovery_process` |
| `clipboard` | Reads or writes the system clipboard | `folder_grid`, `input_ui` |

Common commands:

```sh
ctest -L fast --output-on-failure        # quick loop while you work (about 45 s on a 4-vCPU Linux VM)
ctest -LE budget --output-on-failure     # everything except the timing-budget tests (36 tests)
ctest -L ui --output-on-failure          # the real-QML tests only
ctest -N -L budget                       # list the tests a label selects, without running them
```

`-L` and `-LE` take a regular expression; use `-L '^ui$'` if a future label could contain `ui`.

- **Tests with labels can run in parallel.** Budget tests hold `RESOURCE_LOCK budget` and clipboard tests hold `RESOURCE_LOCK clipboard`, so `ctest -j N` never runs two of either kind at once. Budget tests still compete with everything else running on the machine, so run them serially, or with nothing heavy alongside, when timing matters.
- **These subsets do not replace the full suite.** Before a handoff, run the full suite (`ctest` with no label), then `scripts/build.ps1 -Test` on Windows. The authoritative gate always runs everything.
- **When you add a test,** add its name to at least one list in `cmake/TestLabels.cmake`. Put it in the `budget` list if it asserts timing, and in the `clipboard` list if it touches the system clipboard.
