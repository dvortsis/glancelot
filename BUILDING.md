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

Provide Qt 6.8.3 development/private headers, Quick Controls/QML Dialogs runtime modules, ShaderTools, CMake, Ninja and a C++20 compiler. Source/binary Qt builds may be used; do not mix toolkit versions.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Core-only CI needs no Qt: pass `-DGLANCELOT_BUILD_VIEWER=OFF`. Native Wayland/X11, HDR and physical-monitor accuracy require real desktop hardware and are not validated by a headless build.

## Local evidence

`glancelot image.png --quit-after 3000 --screenshot capture.png --report report.json` runs a timed viewer and records local evidence. `GLANCELOT_CONFIG_DIR` redirects preferences for disposable tests. No network requests are made by the application.

## Docker browser desktop

See [docs/DOCKER.md](docs/DOCKER.md) for the Linux amd64 Qt 6.8.3 multi-stage build, authenticated browser desktop and read-only photo mounts. The Docker build runs the full CTest suite.
