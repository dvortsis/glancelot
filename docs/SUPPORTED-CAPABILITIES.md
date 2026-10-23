# Current supported capabilities

What the current alpha on `main` does, per platform, and what is still unverified. It describes delivered behaviour, not the full requirement inventory in [REQUIREMENTS.md](REQUIREMENTS.md). Every row names the code or the verification record it rests on. When this page and the code disagree, the code wins; please report the mismatch.

Last checked against `main` at `f325ef6` (October 22, 2026).

## Platforms at a glance

| Platform | Status | How it is tested | Evidence |
| --- | --- | --- | --- |
| **Windows 11 x64** | Primary platform. Native C++20 / Qt 6.8.3 Quick app with a GPU canvas, plus a portable ZIP. | Full suite through `scripts/build.ps1 -Test` on the gate machine: 43 of 43 on `ec05e42`. Later runs with unchanged product and test code hit timing failures in `folder_tree`, `folder_grid` and `fs_watch` under load (`docs/verification/gate-triage-2026-10-22/README.md`; packet EN-11). A clean-machine install has not been qualified. | `docs/verification/lead-integrate-all-2026-10-22/README.md`; `BUILDING.md` |
| **Linux, Docker browser desktop** (amd64) | Supported. The same Qt app runs on a Linux server and is streamed to a browser through TigerVNC/noVNC. | `docker build` compiles the app and runs the full suite offscreen: 43 of 43 on `ec05e42`. | `Dockerfile`; `docs/DOCKER.md`; `docs/verification/DOCKER-BROWSER.md` |
| **Linux, native desktop** | Experimental. A local X11/XWayland package for Ubuntu 24.04 x86_64 exists. No native Wayland plugin. | Package built and smoke-tested in a separate Ubuntu runtime with software OpenGL: PNG and JPEG decode and real screenshot colours. No test on a physical desktop. | `tools/platform/LINUX-PACKAGE.md`; `docs/verification/PLATFORM-01.md` |
| macOS | Not supported. | None. | — |

The core library (navigation, sorting, file planning) also builds and runs its tests without Qt on GitHub's `ubuntu-24.04` and `windows-2022` runners (`.github/workflows/build.yml`, job `core`).

## Image formats

Decoding happens in a separate `glancelot-decoder` process. It sniffs the file content, checks it against an allowlist, refuses images whose decoded size would exceed 256 MiB (about 64 megapixels in RGBA), and converts the pixels to 8-bit sRGB (`src/worker/main.cpp`). Folder browsing only lists files whose extension is on the navigation allowlist (`imageCategory` in `src/core/navigation.cpp`).

Each format needs a Qt image plugin:

- **Windows.** Local Windows builds and the `build.yml` Windows job install Qt's `qtimageformats` module (`BUILDING.md`, `.github/workflows/build.yml`). The portable package copies the installed plugins through Qt's deploy script (`CMakeLists.txt`, `qt_generate_deploy_qml_app_script`). The authoritative self-hosted gate job uses the main checkout's local Qt, which includes these plugins.
- **Hosted advisory Windows job.** The hosted job in `windows-gate.yml` installs `qtimageformats` too (EN-01, with EN-02's integration).
- **Docker.** Since EN-02 the image installs `qtimageformats` with `qtbase`, `qtdeclarative`, `qtshadertools` and ICU (`Dockerfile`, `qt` stage), and the runtime stage checks that the WebP, TIFF and TGA plugins and their libraries are present. `AnimationTests::testMultipageTiffNavigation` passes in the Docker build stage (`docs/verification/EN-02-REVIEW.md` on `claude/review-en-02`).

The Windows and Docker columns below therefore match for formats.

| Format | Extensions | Windows | Docker browser desktop | Evidence and limits |
| --- | --- | --- | --- | --- |
| JPEG, PNG | `.jpg` `.jpeg` `.png` | Yes | Yes | EXIF orientation applied; embedded ICC profile converted to sRGB. Decoder acceptance (DECODE-01, state `integration`); Linux package smoke used PNG and JPEG. |
| BMP | `.bmp` | Yes | Yes | `FormatTests::testBmpDecode` (`tests/format_tests.cpp`). |
| GIF, still and animated | `.gif` | Yes | Yes | Playback with per-frame delays, speed 0.1x to 4x, loop and frame stepping: `AnimationTests::testAnimatedGifPlayback` (`tests/animation_tests.cpp`). |
| WebP, still and animated | `.webp` | Expected (plugin present); **no decode test** | Expected (plugin present); **no decode test** | The extension is listed and the decoder allows it. No WebP fixture is decoded in any test. Animated WebP playback goes through the same code path as GIF but is **unverified**. |
| APNG | `.apng` | **Unverified** | **Unverified** | The extension is listed. Qt's PNG reader decodes it, but no test shows that more than the first frame plays. |
| TIFF, including multipage | `.tif` `.tiff` | Yes, from Qt's plugin | Yes, from Qt's plugin | Multipage TIFF opens as pages with no invented timing: `AnimationTests::testMultipageTiffNavigation`, which runs on Windows and in the Docker build. |
| TGA | `.tga` | Expected (plugin present); **no decode test** | Expected (plugin present); **no decode test** | Extension and allowlist only. |
| RAW camera files (embedded preview only) | `.dng` `.cr2` `.cr3` `.nef` `.nrw` `.arw` `.sr2` `.raf` `.orf` `.rw2` `.pef` `.iiq` | Yes, preview only | Yes, preview only | Glancelot shows the largest embedded JPEG preview, not the developed RAW data (`src/app/raw_preview.cpp`). Tests use synthetic TIFF-structured and scanned fixtures, plus missing-preview and malformed inputs (`tests/format_tests.cpp`; `docs/verification/FORMAT-02.md`). No real camera files are tested; the per-camera matrix in FMT-06 is open. |
| ICO, WBMP | `.ico` `.wbmp` | **No** | **No** | The decoder accepts the content, but the extensions are not on the navigation allowlist, so these files do not appear in folders, the grid or the filmstrip. Opening one directly is untested. |
| HEIC, AVIF, JPEG XL, QOI, EXR, PSD, archives, video | — | No | No | Not implemented (FMT-04, FMT-05 and later rows in REQUIREMENTS.md). |

Known format limits, from the UX audit of `6cc87ee` (`docs/verification/AUDIT-UX-6cc87ee.md`):

- **Large images:** an image over the 256 MiB limit is refused with a message. There is no reduced-size fallback yet.
- **Damaged files:** a damaged or non-image file is reported with the "This alpha supports JPEG, PNG, GIF, WebP, TIFF, BMP, and TGA images" message rather than as damaged.
- **Animation decoding:** animation frames are decoded in the viewer process rather than the decoder process, with no byte cap on the 64 pre-decoded frames. Large animations can use a lot of memory.

## Viewing

| Feature | Windows | Docker | Evidence |
| --- | --- | --- | --- |
| Fit modes (shrink-only, fit, fill, width, height), pan, cursor-anchored zoom, rotate view, background cycle | Yes | Yes | `tests/canvas_tests.cpp` (CTest `viewport`); CANVAS-UI (state `integration`, physical qualification open) |
| Actual size (one image pixel per physical display pixel) and hold-to-loupe | Formula tested | Not meaningful (fixed virtual desktop scaled by noVNC) | `tests/canvas_tests.cpp` (CTest `viewport`). Physical 1:1 at fractional DPI is **unverified**. |
| Metadata inspector (EXIF, IPTC, XMP; key **I**) | Yes | Yes | `docs/verification/META-HUD-01.md` |
| Folder grid, filmstrip, lazy folder tree, path bar, favorites, back/forward history, filter (**Ctrl+F**) | Yes | Yes | `docs/verification/FILE-GRID-01.md`, `TREE-01.md`, `PATH-BAR-01.md`, `FAVORITES-01.md`, `FOLDER-HISTORY-01.md`, `FILTER-01.md` |
| File order: name, modified, size, seeded shuffle; folder order independent of file order | Yes | Yes | SORT-01; `tests/navigation_tests.cpp`. Capture-date and dimension sorts are **not delivered**. |
| Live folder updates and explicit refresh (**F5**) | Yes | Yes | `docs/verification/FS-WATCH-01.md` (FS-WATCH-01, state `done`) |
| Per-window resume of the last photo, by window opening order | Yes | Each window's last folder and photo are stored in the `glancelot-config` volume; resume after recreating the container is **unverified** | WINDOW-RESUME-01; `docs/verification/DOCKER-BROWSER.md` (its "not verified" list) |
| Remappable commands, command palette (**Ctrl+Shift+P**), shortcut cheat sheet (**F1**), scoped keyboard/mouse/wheel input, chords and holds | Yes | Yes; keyboard input through noVNC is unverified (DOCKER-BROWSER-01) | `docs/verification/INPUT-02.md`, `SHORTCUTS-HELP-01.md` |
| 80 built-in themes (40 families, light and dark) | Yes | Yes | `docs/verification/THEMES.md` |
| Colour | 8-bit sRGB working pixels; the main canvas is SDR and does not use the monitor's ICC profile | Same | `src/worker/main.cpp`; COLOR-01 and COLOR-04 are open |

### Merged, corrections outstanding

These features were merged with the Claude stack at `3b2b0db` (October 22, 2026). They build and pass the full suites on `ec05e42`. They are not yet done: frozen corrections and a fresh confirmation review are outstanding (STACK-CORRECTION-01; `docs/handoff/LEAD-WRAPUP-2026-10-22.md`). On `f325ef6` the four correction packets (STACK-F0xx) are in review and STACK-CONFIRM has not started. Expect changes.

| Feature | What exists | Open issue |
| --- | --- | --- |
| Breadcrumbs and path completion (F002) | Path bar breadcrumbs, cancellable completion | Confirmation only |
| Zoom presets, zoom lock, navigator (F037) | 50/200/400% presets, keep zoom across photos | Confirmation only |
| Slideshow (F031) | Start/stop, faster/slower | Confirmation only |
| Reference window (F040) | Pin a photo in a separate window | Must share memory accounting with the second-display "pin" output (STACK-F040-PINNED) |
| Focus mode and named layout presets (F049) | Hide bars and panels; cycle presets | Must obey layout lock and keep unknown settings (STACK-F049-PRESETS) |
| Details view in the grid (F009) | Details or thumbnails toggle | Confirmation only |
| Grid badges (F014) | File-type and failed-preview badges | Rating, dimension and quality badges not delivered |
| Shortcut recorder (F051) | Record chords, explain conflicts | Confirmation only |
| Accessibility audit (F052) | Every interactive control has an accessible name in tests | No NVDA/Orca trial |
| High contrast and large targets (F053) | High-contrast palette, 44 px targets | OS high-contrast detection not delivered |
| Localization and right-to-left (F054) | Pseudo-locale catalog, RTL mirroring | Newer strings not yet wrapped (STACK-F054-COVERAGE) |
| Tabs (F015) | Tab strip with saved locations | Tabs do not persist through crash recovery yet (STACK-F015-RECOVERY) |
| File-type filter chips (F042, chips only) | Type chips in the grid header | Rating, orientation and date chips not delivered |

## File operations

Viewing never writes to your photos. Every change below happens only on an explicit command, and each command is remappable:

- **Confirmed first:** delete, rename and new folder open a dialog (`ui/DeleteFlow.qml`, `ui/RenameFlow.qml`).
- **Runs at once:** paste starts immediately, with no confirmation.

| Operation | Default key | Windows | Native Linux | Docker | Evidence |
| --- | --- | --- | --- | --- | --- |
| Delete to Recycle Bin / Trash (selection, current photo, current folder) | **Delete**, **Ctrl+Delete**, **Ctrl+Shift+Delete** | Recycle Bin through the Windows shell; an item that would be deleted permanently is refused | freedesktop.org Trash (`QFile::moveToTrash`) | `/photos` is mounted read-only, so deletion is refused there (inferred, not observed) | `src/app/platform_trash.hpp`; `docs/verification/DELETE-01.md`, `DELETE-KEYS-01.md` |
| Rename file or folder; new folder | **F2**; **Ctrl+Shift+N** | Yes | Yes (tests run in Docker) | Read-only mount: not possible in `/photos` (inferred, not observed) | `docs/verification/RENAME-01.md` |
| Copy, cut, paste files in a background queue with pause, resume and cancel | **Ctrl+X**, **Ctrl+V** (copy has no default key) | Yes | Yes (tests run in Docker) | Only into a writable mount | Paste always keeps both files on a name clash: the new copy gets a numbered name and the existing file is untouched (`CopyMoveService::pasteTo` uses `CollisionPolicy::RenameBoth`, `src/app/copy_move_service.cpp`). Cross-volume moves copy, check the size, then remove the source; the check compares sizes only, not content. `docs/verification/COPY-MOVE-01.md` |
| Copy file path, copy file, copy image to the clipboard | **Ctrl+C**, **Ctrl+Shift+C**, **Ctrl+Alt+C** | Yes | Yes | Inside the remote desktop only | `docs/verification/CLIPBOARD-OPS-01.md` |
| Reveal in file manager; open terminal here | **Ctrl+Alt+R**; **Ctrl+Alt+T** | Explorer; Windows Terminal or PowerShell | `xdg-open`; `$TERMINAL` or a known emulator | No file manager or terminal is installed in the image | `docs/verification/PLATFORM-OPS-01.md`, `PLATFORM-TEST-ENV-01.md` |

Known issue, COPY-SAFETY-01 (open, `ready`):

- The copy service also has Skip and Overwrite policies, but no UI reaches them today.
- Its Overwrite path truncates an existing target before copying, and deletes the target if the copy fails or is cancelled (`src/app/copy_move_service.cpp`).
- The fix is queued; until it lands, Overwrite is unsafe.

Not delivered yet: undo, an operation journal, batch rename, drag and drop, and grouping a RAW file with its JPEG or sidecar (tracks B1 to B6 in `docs/handoff/LEAD-PLAN-2026-10-22.md`). On a read-write Docker mount, where the Trash ends up is **unverified**.

## Second display (monitor output)

| Feature | Windows | Docker | Evidence |
| --- | --- | --- | --- |
| Presentation window on another display: mirror, pin, compare (**F8** toggles it) | Yes | Not applicable (one virtual display) | MONITOR-OUTPUT-01, state `done`; `docs/verification/integration-2026-10-21/monitor/README.md` |
| Per-output fit, actual size, zoom, pan, linked comparison viewports and colour path | Yes, software-verified | Not applicable | Same; native shader proofs at DPR 1 and 2 |
| Display picker, identify, swap, move, disable, recover; detachable tree/grid/preview/metadata panes; workspace presets | Yes | Detaching panes in the remote desktop is **unverified** | MONITOR-CUSTOM-01, state `done`; `docs/handoff/MONITOR-CUSTOM-01-CODEX-HANDOFF.md` |

Physical accuracy of all of the above (real monitors, calibration, hotplug) is **unverified**. MONITOR-VERIFY-01 is still `ready`.

## Safe mode and recovery

| Feature | How to use it | Evidence |
| --- | --- | --- |
| Safe mode | `glancelot --safe-mode`: isolated temporary settings, and it requests a software rendering device. Your saved settings are left untouched. | `app/startup_intent.cpp`; `docs/verification/SAFE-MODE-01.md` |
| Reset damaged settings | `glancelot --reset-settings` sets damaged or incompatible settings aside and starts with defaults. | `app/startup_intent.cpp` |
| Settings backup, preview and restore | `--settings-backup <new-dir>`, `--settings-diff <package>`, `--settings-restore <package> --settings-preview <sf1 fingerprint>`, optionally limited by `--settings-scope` and `--settings-slot` | `app/startup_intent.cpp`; SAFE-MODE-01, state `done` |
| Atomic settings writes with a last-good copy | Automatic | SYS-03; `docs/verification/SAFE-MODE-01.md` |
| Crash recovery of each window's last folder and photo | Automatic on next start | SAFE-MODE-01 recovery engine. There is no in-app restore screen yet (F057 UI, packet B9). |

Hardware power-loss and sanitizer runs for recovery are **unverified**.

## Docker browser desktop specifics

See [DOCKER.md](DOCKER.md) for setup.

- **Read-only photos.** Photos are mounted read-only at `/photos`, so delete, rename and moving files into that tree should be refused. This is inferred from the mount and the delete planner, not observed.
- **Settings.** Settings and each window's last folder and photo live in the `glancelot-config` volume. Resume after recreating the container is unverified.
- **One shared desktop.** All browser clients share one desktop and one viewer session. Browser file upload and drag-in are not implemented.
- **Fixed desktop size.** The remote desktop has a fixed size (`GLANCELOT_DESKTOP_SIZE`, 560x400 to 3840x2160) and renders with Mesa software OpenGL. Physical-pixel zoom, colour accuracy and refresh rate are not meaningful through this route.
- **WebP, TIFF and TGA.** The image includes Qt's `qtimageformats` module (EN-02); multipage TIFF is tested in the Docker build, WebP and TGA have no decode test on any platform. See [Image formats](#image-formats).
- **Authentication.** Only classic VNC password authentication is offered. It does not encrypt traffic. Use a trusted LAN or VPN, or a reverse proxy with authenticated HTTPS.
- **Unverified container behaviour.** Not verified: pixel-level browser clicks, `docker compose` itself, resume after container recreation, Unraid, ARM and GPU acceleration (DOCKER-BROWSER-01 in the work queue; `docs/verification/DOCKER-BROWSER.md`).

## Unverified on physical hardware

None of the following has been measured on real hardware. Headless and software-rendered results are never relabelled as physical.

| Area | Status | Tracked by |
| --- | --- | --- |
| Mixed DPI and fractional scaling (125/150/175%) on real monitors; physical 1:1 pixel check | Unverified | VIEW-04, CANVAS-UI, MONITOR-VERIFY-01 |
| ICC colour management on the main canvas | Not implemented (SDR sRGB only) | COLOR-01; packet D4 |
| Per-monitor ICC accuracy on the second-display output | Software path only; unverified | MONITOR-VERIFY-01 |
| HDR output, wide gamut, high bit-depth sources | Not implemented | COLOR-04 |
| High refresh rate (120/144 Hz) presentation and frame pacing | Unverified | PERF-RENDER; packet D3 |
| Native Linux on X11 and Wayland compositors, GPU acceleration, other distributions | Unverified; no Wayland plugin in the package | PLATFORM-01, MONITOR-VERIFY-01 |
| Monitor hotplug, suspend/resume, GPU loss | Unverified | MONITOR-VERIFY-01 |
| Screen readers (NVDA, Orca), real keyboard layouts and IME, trackpad gestures | Unverified | F052, IN-03 |
| Clean-machine Windows install; HDD and SMB share performance | Unverified | PLATFORM-01; packet F6 |

## Not part of the viewer

No telemetry, cloud account, mandatory catalog, automatic updates or paid service. Process separation of the decoder is not a full OS sandbox.
