# Glancelot

A native, local-first photo viewer for Windows and Linux. Built with C++20, Qt Quick and a dedicated GPU canvas. Early alpha: JPEG and PNG, deterministic depth-first folder navigation, fit/pan/zoom, physical-pixel actual size, hold loupe, remappable keyboard commands, palette and preferences, worker-backed filmstrip, lazy folder tree, and independent per-window location resume.

## Run

Windows portable package: extract `dist/Glancelot.zip` and run `bin/glancelot.exe`. Keep `glancelot-decoder.exe` beside it. Open with Ctrl+O or drop a JPEG/PNG. Right/Left browse; Page Down/Up jump folders; F fits; 1 shows actual size; R rotates the view; F11 fullscreen; Ctrl+Shift+P opens commands. Drag to pan, Ctrl+wheel zooms, right-button hold shows the loupe. Settings remap commands and wheel/boundary behavior. T toggles the filmstrip; Ctrl+B shows the folder tree. Each window remembers its own last successfully opened photo by opening order; explicit file arguments override resume.

## Appearance

Use **Themes** in the toolbar or **Settings → Appearance**. Forty built-in families provide eighty light/dark variants, with live search, swatch previews, immediate application and saved preferences. Follow system, force dark, or force light. Slate & Ice uses the exact supplied palette and original knight assets. Themes affect interface chrome; the photo canvas keeps its independent neutral background and source pixels are unchanged.

Families: Slate & Ice, Graphite, Arctic, Ocean, Cobalt, Indigo, Iris, Lavender, Plum, Orchid, Rose, Cranberry, Ruby, Coral, Terracotta, Copper, Amber, Sandstone, Honey, Olive, Moss, Fern, Forest, Emerald, Jade, Lagoon, Teal, Glacier, Steel, Storm, Midnight, Lilac, Heather, Mulberry, Blush, Clay, Espresso, Parchment, Sage, Monochrome.

Preferences: normal OS application-config directory, or `GLANCELOT_CONFIG_DIR` for portable/test use. Decoder is a separate process with bounded pixel transfer, format allowlist and timeout. Process separation is not yet a complete OS sandbox.

## Development and status

See [BUILDING.md](BUILDING.md), [execution plan](docs/AGENT-BUILD-PLAN.md), [work queue](docs/WORK-QUEUE.yaml), and [full requirements](docs/REQUIREMENTS.md). The full roadmap remains in progress. RAW, XMP culling, comparison, archive reading, HDR, calibrated monitor output and complete input-matrix support are not delivered by this alpha. Native Linux desktop/GPU behavior remains unqualified; Linux core tests run in WSL.

No telemetry, cloud, mandatory catalog or automatic updates. Source under GPL-3.0-or-later; see LICENSE and THIRD_PARTY_NOTICES.md. Brand assets were supplied by the project owner; their provenance is preserved in assets/brand.
