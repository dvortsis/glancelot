# Second-monitor support and customization

Approved roadmap addition: October 18, 2026. Implements the existing UX-10 requirement in M5; this document is a plan, not a delivered capability. Native Windows and Linux are the qualification targets. The current single-display Docker/noVNC desktop requires a separate capability gate before exposing multi-output controls.

## Workflows and controls

- Keep the tree, grid, metadata, selection and Settings on a controller display while presenting a clean image on a chosen second display.
- Extend the pane contract to detachable/re-dockable tree, grid, preview and metadata panes across displays. Offer per-pane freeze and a global layout lock, named workspace presets, keyboard move-to-display commands and a reset-to-controller recovery action. Preserve each pane's selection and focus when re-docking; this extends PANES-01's initial in-window layout.
- Choose mirror-current-image, pinned reference image, or comparison output. Selection and playback synchronization are explicit; pinned output stays unchanged until replaced or unpinned.
- Choose target display by a stable identity where available, with a readable name, resolution, scale and connection status. Offer Identify displays, Swap controller/output, Move output, Enable/disable output and an always-reachable recovery command.
- Customize output fullscreen/windowed mode, fit/fill/actual-size, independent or linked zoom/pan/rotation, background/checker, guides, image information and cursor/overlay visibility. A clean presentation preset hides controller menus and notifications by default.
- Save named presentation presets and per-window output preferences. Configure whether a preset follows the selected image, what it does on folder changes, and whether slideshow timing is shared. Never automatically enable a previously disconnected output solely because its numeric screen index was reused.
- Show the output's actual color/profile/precision capability and limitations. Per-output scale and color transforms must not be inherited blindly from the controller. SDR output remains usable when HDR or profile qualification is unavailable.

## Ownership and safety

The controller owns navigation and explicit commands; each output owns its viewport and rendering resources. Decode immutable source/revision pixels once when compatible, then apply output-specific transforms. Count all surfaces, uploads and textures against aggregate CPU/GPU budgets. Slow or unavailable outputs must not block controller input.

The lead freezes output identity, routing, viewport-link and persisted-preset contracts before shared headers change. Async results carry source and selection generations; an older selection cannot replace either output. Closing an output never deletes a file or exits the controller. Escape dismisses presentation predictably; commands respect text fields and dialogs.

On unplug, suspend, resolution/DPI change, compositor restart or GPU loss, preserve navigation, return reachable controls to an available display, release obsolete resources and show useful status. A missing display never places the only Settings/recovery window offscreen. Restoring a layout clamps geometry to current work areas. Distinguish a same-model monitor from a truly stable identity and fall back conservatively when platform identifiers are weak.

## Delivery packets

1. **MONITOR-OUTPUT-01:** output-window lifecycle, capability enumeration, explicit routing and clean rendering. Depends on GRID-PREVIEW-01, PLATFORM-01 and CACHE-01.
2. **MONITOR-CUSTOM-01:** display picker, detachable pane routing, freeze/lock and workspace presets, output appearance/viewport synchronization and recovery commands. Depends on MONITOR-OUTPUT-01 and UI-SETTINGS-01.
3. **MONITOR-VERIFY-01:** independent native UI/UX, resource, hotplug and physical output qualification. Depends on both implementation packets. Only the declared tested platforms/capabilities become supported.

## Acceptance and verification matrix

| Area | Required cases and evidence |
| --- | --- |
| Display lifecycle | One/two/three displays, missing preferred output, same-model monitors, disconnect/reconnect, primary-display change, output close and controller close; no inaccessible windows or state loss. |
| Input and isolation | All routes by pointer, keyboard and palette; controller dialogs never appear on clean output; focus never jumps unexpectedly; Escape/recovery works from every mode. |
| Customization | Every output setting, named preset create/rename/delete/reset, pin/unpin, synchronized/independent viewport, fullscreen/windowed and restore after restart; settings remain isolated from other window slots. |
| Image state | Rapid navigation, failed decode, stale preview, sort/filter changes and pinned-reference removal; correct image/revision on every output. |
| Display accuracy | 100/125/150/175/200% scale and mixed-DPI moves, landscape/portrait, differing resolutions/refresh, physical 1:1 checkerboards and qualified per-monitor ICC transforms. HDR remains conditional on calibrated evidence. |
| Resource bounds | 2-up/4-up plus output, shared/dedicated GPU memory, low memory, background controller and repeated open/close; bounded allocations and no sustained leak. |
| Recovery | Suspend/resume, device loss, resolution change, unavailable compositor operations, corrupt presets and interrupted/concurrent preference writes. |
| UI craft | All screens, menus and presets at supported compact/wide sizes in dark/light themes, large text, keyboard-only and relevant assistive technology; independent design-review. |
| Platform boundary | Windows native; Linux Wayland/X11 separately; virtual displays/CI only prove corresponding functional logic. Docker/browser output remains unsupported unless its virtual-display and client-routing design is independently qualified. |

Use generated media, exact commands and output, screenshots/traces and a platform-by-platform results table. Hardware-dependent acceptance remains explicitly unverified when the equipment is unavailable. No screenshot alone proves physical color accuracy or refresh performance.
