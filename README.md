# Glancelot

A native, local-first photo viewer for Windows and Linux. Built with C++20, Qt Quick and a dedicated GPU canvas. Early alpha: JPEG, PNG, BMP, GIF (animated) and embedded previews of common RAW camera files, plus TIFF (multipage), WebP and TGA in the Windows build (not yet in the Docker image), deterministic depth-first folder navigation, fit/pan/zoom, physical-pixel actual size, hold loupe, remappable keyboard commands, palette and preferences, worker-backed filmstrip, lazy folder tree, and independent per-window location resume.

## Try the current build

On Windows, double-click **Test Glancelot.cmd** to build, run tests, package and open this checkout. Afterward, **Open Last Test Build.cmd** reopens that tested build immediately. Test preferences are separate from normal settings. See [testing instructions](docs/TESTING.md) for version details and command-line options.

## Run

Windows portable package: extract `dist/Glancelot.zip` and run `bin/glancelot.exe`. Keep `glancelot-decoder.exe` beside it. Open with Ctrl+O or drop a supported image ([formats per platform](docs/SUPPORTED-CAPABILITIES.md#image-formats)). Right/Left browse; Page Down/Up jump folders; F fits; 1 shows actual size; R rotates the view; F11 fullscreen; Ctrl+, opens Settings; Ctrl+Shift+P finds a command (Settings → Controls). Drag to pan, Ctrl+wheel zooms, right-button hold shows the loupe. Settings → Controls lists every command: search it, run one with Enter, and remap or clear its shortcut. Settings also hold wheel, folder-boundary, file-order and panel behavior. S cycles file order (name, modified, size, shuffle); Shift+S reverses it. T toggles the filmstrip; Ctrl+B shows the folder tree. File changes happen only on explicit commands with confirmation: Delete moves the selection to the Recycle Bin/Trash, F2 renames, Ctrl+X then Ctrl+V moves files. Each window remembers its own last successfully opened photo by opening order; explicit file arguments override resume.

## Appearance

Open **Settings → Appearance**. Forty built-in families provide eighty light/dark variants, with live search, swatch previews, immediate application and saved preferences. Follow system, force dark, or force light. Slate & Ice uses the exact supplied palette and original knight assets. Themes affect interface chrome; the photo canvas keeps its independent neutral background and source pixels are unchanged.

The interface is built on Qt Quick Controls' Basic style. Shared design tokens (palette roles, spacing, radii, control sizes, type scale, motion) live in `ui/Theme.qml`, and the `Ui*` components in `ui/` are the canonical button, field, choice list, switch, dialog, scroll bar and icon set; new UI should use them rather than restyling controls in place. Interface type is Geist and Geist Mono (SIL OFL 1.1), bundled under `assets/fonts/geist`.

Families: Slate & Ice, Graphite, Arctic, Ocean, Cobalt, Indigo, Iris, Lavender, Plum, Orchid, Rose, Cranberry, Ruby, Coral, Terracotta, Copper, Amber, Sandstone, Honey, Olive, Moss, Fern, Forest, Emerald, Jade, Lagoon, Teal, Glacier, Steel, Storm, Midnight, Lilac, Heather, Mulberry, Blush, Clay, Espresso, Parchment, Sage, Monochrome.

Preferences: normal OS application-config directory, or `GLANCELOT_CONFIG_DIR` for portable/test use. Decoder is a separate process with bounded pixel transfer (256 MiB decoded limit), content-sniffed format allowlist and timeout. Process separation is not yet a complete OS sandbox.

## Development and status

See [current capabilities](docs/SUPPORTED-CAPABILITIES.md), [BUILDING.md](BUILDING.md), [execution plan](docs/AGENT-BUILD-PLAN.md), [work queue](docs/WORK-QUEUE.yaml), and [full requirements](docs/REQUIREMENTS.md). The full roadmap remains in progress. Full RAW development (only embedded previews are shown), XMP culling, in-app comparison, archive reading, HDR, ICC-managed canvas output and physical monitor qualification are not delivered by this alpha. The full test suite runs on Windows and on Linux (offscreen, inside the Docker build); native Linux desktop and GPU behavior remain unqualified. The current plan is [docs/handoff/LEAD-PLAN-2026-10-22.md](docs/handoff/LEAD-PLAN-2026-10-22.md).

No telemetry, cloud, mandatory catalog or automatic updates. Source under GPL-3.0-or-later; see LICENSE and THIRD_PARTY_NOTICES.md. Brand assets were supplied by the project owner; their provenance is preserved in assets/brand.

Browser access from a Docker server: see [Docker setup](docs/DOCKER.md). The Folders button works before a photo is open and lists available filesystem locations.
