# Build Glancelot

Baseline: C++20, Qt 6.8.3 (Core, Gui, Quick, QuickControls2, Concurrent, Test and ShaderTools), CMake >=3.24, Ninja. Custom rendering uses the pinned Qt private QRhi API. Development packages need private Qt headers.

## Windows

For routine manual testing after setup, double-click **Test Glancelot.cmd**. It runs the build/test/package workflow below and opens the result with isolated test preferences. **Open Last Test Build.cmd** skips rebuilding. See [docs/TESTING.md](docs/TESTING.md).

Install Visual Studio 2022 C++ Build Tools (MSVC x64 and Windows SDK). The implementation environment uses an existing installation. All additional dependencies are workspace-local:

```powershell
$env:UV_CACHE_DIR = "$PWD/.tools/uv-cache"
uv venv .tools/python
uv pip install --python .tools/python/Scripts/python.exe cmake==4.4.4 ninja==1.13.2 aqtinstall==3.3.0
.tools/python/Scripts/python.exe -m aqt install-qt --outputdir .tools/Qt windows desktop 6.8.3 win64_msvc2022_64 --archives qtbase qtdeclarative
.tools/python/Scripts/python.exe -m aqt install-qt --outputdir .tools/Qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools qtimageformats --archives qtshadertools qtimageformats
./scripts/build.ps1 -Test
./scripts/build.ps1 -Test -Package
```

Build script imports MSVC environment and local Qt. Run `dist/Glancelot/bin/glancelot.exe` (or deployed executable location reported by install) after packaging. For a developer launch, put the Qt bin directory on PATH and launch `build/glancelot.exe`. The decoder executable must remain beside the viewer.

Core-only validation: `./scripts/build.ps1 -BuildDir build-core -CoreOnly -Test`.

## Linux

On Ubuntu 24.04 one command installs everything, builds and tests. It mirrors the Dockerfile `qt` and `build` stages, so a cloud or desktop session gets the same Qt and image formats (WebP, TIFF and TGA through `qtimageformats`, as on Windows):

```sh
scripts/linux-bootstrap.sh
```

The script is idempotent; rerun it after pulling. It:

1. Prints whether a Docker daemon is reachable. Without one, only offscreen CTest runs locally and the Docker build happens in the authoritative gate (`lead-windows-gate`, docs/CI-GATE-RUNNER.md).
2. Installs the apt packages it is missing, through `sudo` when not root: build-essential, CMake, Ninja, Python3 (with venv), Xvfb, Mesa GL/EGL and the XCB/xkbcommon/fontconfig/D-Bus/GLib runtime libraries. `cmake/RecoveryTests.cmake` requires Python3 and Xvfb. Nothing else needs root.
3. Installs Qt 6.8.3 `linux_gcc_64` with aqtinstall 3.3.0 into `~/Qt/6.8.3/gcc_64`, unless it is already there: modules `qtshadertools qtimageformats`, archives `qtbase qtdeclarative qtshadertools qtimageformats`, then `icu`. aqt needs HTTPS access to download.qt.io (or a mirror given with `--qt-base`).
4. Configures and builds `build/` (Release, `BUILD_TESTING=ON`) with Ninja.
5. Runs CTest with `QT_QPA_PLATFORM=offscreen`: `ctest -L fast` when that label exists, otherwise the full suite.

| Option | Default | Use |
| --- | --- | --- |
| `--qt-prefix DIR` | `$GLANCELOT_QT_PREFIX` or `~/Qt` | Qt install root |
| `--qt-base URL` | `$GLANCELOT_QT_BASE` or aqt's default | Qt download mirror |
| `--build-dir DIR` | `$GLANCELOT_BUILD_DIR` or `build` | Build directory; track sessions use `build-claude-<task>` |
| `--jobs N` | `$GLANCELOT_JOBS` or 4 | Parallel compile jobs |
| `--debug` | Release | Debug build |
| `--skip-apt` | | Skip system packages (other distributions: install the equivalents yourself) |
| `--qt-only` / `--no-tests` | | Stop after Qt, or after the build |
| `-- <ctest args>` | | Replace the default test selection, for example `-- -R animation` |

Manual equivalent with an existing Qt 6.8.3 (development and private headers, Quick Controls, ShaderTools and image formats; do not mix toolkit versions):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Core-only CI needs no Qt: pass `-DGLANCELOT_BUILD_VIEWER=OFF`. Native Wayland/X11, HDR and physical-monitor accuracy require real desktop hardware and are not validated by a headless build.

## Local evidence

`glancelot image.png --quit-after 3000 --screenshot capture.png --report report.json` runs a timed viewer and records local evidence. `GLANCELOT_CONFIG_DIR` redirects preferences for disposable tests. No network requests are made by the application.

## Docker browser desktop

See [docs/DOCKER.md](docs/DOCKER.md) for the Linux amd64 Qt 6.8.3 multi-stage build, authenticated browser desktop and read-only photo mounts. The Docker build runs the full CTest suite.
