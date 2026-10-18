# Current supported capabilities

This page describes the current alpha rather than the full requirement inventory. See the work queue and verification records for commits and platform evidence.

- Windows 11 x64: native C++20 / Qt 6.8.3 Quick viewer and portable package; local MSVC 2022 build. A same-machine portable launch with development Qt removed from PATH is tested. A clean-machine install is not yet qualified.
- JPEG and PNG still images: separate decoder process, content sniffing/allowlist, bounded dimensions/transfer, EXIF orientation and embedded-profile conversion into 8-bit sRGB working pixels. Source high precision is not preserved in that working format. No animation, multipage, RAW, archive or other extension support is advertised.
- Viewing: shrink/fit/fill/width/height, pan/zoom, physical-pixel actual-size formula, hold loupe, view rotation and background cycling. Physical mixed-DPI, ICC output and HDR qualification remain open.
- Browsing: natural folder traversal, boundary policies, filmstrip, lazy folder tree, virtualized folder grid, numbered-window photo resume, and name/modified/size/seeded-shuffle file order after audited integration. Capture-date/dimension sorts and filtering are pending.
- Interaction: remappable keyboard commands, command palette, grid/canvas/modal precedence, core wheel modes and settings. Full input contexts/mouse/gesture/chord coverage and assistive-technology qualification are pending.
- Safety: viewing opens source media read-only; generated fixtures are checked for source identity. Settings use coordinated atomic updates with recovery copies. Worker queues/cache have explicit limits; shared current-decode/thumbnail admission and OS sandbox enforcement are pending.
- Linux: core navigation/sort tests run under Ubuntu WSL/GCC. Full Qt GUI/package, compositor, GPU and native interaction are unverified.

The capability boundary remains SDR/sRGB output with an unmanaged display-profile warning. Bundled Qt image plugins do not expand the explicit JPEG/PNG decoder allowlist. No telemetry, cloud account, mandatory catalog or automatic update service is part of the viewer.
