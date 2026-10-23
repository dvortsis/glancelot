# Test the current application

On Windows, double-click **Test Glancelot.cmd** in the checkout root. It builds this checkout, runs the full automated suite, packages the viewer and decoder, and opens the native app. The first build takes longer; later builds are incremental. Visual Studio 2022 C++ Build Tools and the local Qt/tools setup in [BUILDING.md](../BUILDING.md) are required. No dependencies are downloaded by the launcher.

After that, **Open Last Test Build.cmd** opens the last successful test package immediately. It does not rebuild or include later source changes. Use **Test Glancelot.cmd** after pulling or changing code. Close the test app before rebuilding. Build/test failures stop the launch and stay visible in the console.

The console stays open so you can read the result; pressing a key closes the launcher window while the app keeps running. It shows the branch, full commit, whether there were local changes, and the time tests passed. This record is also in `dist/Glancelot/test-build.json`. The fast launcher checks the viewer and decoder hashes so an overwritten package cannot silently impersonate the tested build. These checks identify local builds, not signed releases.

Use Ctrl+O or drop a supported image into the window ([formats per platform](SUPPORTED-CAPABILITIES.md#image-formats)). Ctrl+, opens Settings; Ctrl+Shift+P opens the command palette. Test settings and window resume state are kept separately in `.cache/manual-testing/config` and reused across launches, leaving normal application preferences alone. Photos opened for viewing are not changed; only explicit delete, rename, new-folder or copy/move commands change files (paste runs without confirmation), so test those on disposable copies. The launcher does not reset settings or copy your photo library.

PowerShell equivalents (run from the checkout root):

```powershell
./scripts/test-app.ps1
./scripts/test-app.ps1 -NoBuild
./scripts/test-app.ps1 -Path 'C:\Photos\Example image.png'
./scripts/test-app.ps1 -NoLaunch  # build, test and package without opening a window
```

Each checkout/worktree builds and packages its own current branch under `build-manual-testing` and `dist/Glancelot`. The root shortcuts test the integrated checkout; experimental branches require running their own launcher from their assigned worktree. To test default preferences, close this checkout's test windows and rename `.cache/manual-testing/config` before reopening.

For portable sharing, extract `dist/Glancelot.zip` and run `bin/glancelot.exe`; keep all bundled files together. The test-build record is local and is created after packaging, so it is not included in this ZIP. For the Linux browser desktop, use the [Docker instructions](DOCKER.md); this launcher does not rebuild or update a running Docker deployment.

Useful manual checks: open a generated/disposable photo folder, navigate with arrows, toggle folders (Ctrl+B) and the filmstrip (T), exercise grid/preview and pane layouts, switch themes, then close and reopen to check window resume. Hardware-dependent display/color behavior still requires native testing on the target hardware; see the unverified list in [current capabilities](SUPPORTED-CAPABILITIES.md#unverified-on-physical-hardware).

On Linux, the same full suite runs offscreen inside `docker build` (see [DOCKER.md](DOCKER.md)). That image currently lacks Qt's `qtimageformats` module, so WebP/TIFF/TGA decoding is expected to be absent there (inferred from the Dockerfile) and `testMultipageTiffNavigation` skips itself until packet EN-02 lands.
