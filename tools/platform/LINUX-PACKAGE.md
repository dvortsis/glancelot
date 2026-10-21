# Linux X11 GUI package

This local package targets Ubuntu 24.04 x86_64 with an X11 or XWayland desktop.
Extract it into a new directory and run `./Glancelot.sh [image]`. Keep bin, qt and
qt.conf together. The viewer starts its sibling glancelot-decoder process; do not
move either executable alone. No installation, catalog, network service or updater
is required. Configuration remains per-user; GLANCELOT_CONFIG_DIR can isolate it.

Qt 6.8.3 shared libraries, imported QML modules, X11/image plugins, Qt SPDX SBOMs,
application/third-party notices and the Geist font license are included. Qt libraries
are dynamically linked and may be replaced with compatible builds. System graphics,
font, X11, DBus, GLib and C runtime libraries remain OS dependencies. The build's
qualification/dependencies.json lists actual system SONAME dependencies; the package
was checked in a separate Ubuntu runtime with no development Qt installation.

Ubuntu runtime dependencies used for that check are libgl1-mesa-dri, libegl1, libgl1,
libopengl0, libxkbcommon-x11-0, libfontconfig1, libdbus-1-3, libglib2.0-0t64,
libxcb-cursor0, libxcb-icccm4, libxcb-keysyms1, libxcb-shape0, libxcb-xinerama0,
libxcb-image0, libxcb-render-util0, libasound2t64 and libgssapi-krb5-2, plus a usable
X11 display and fonts.
The launcher never installs packages or changes system configuration. Have the
machine's administrator provision missing OS dependencies through its normal process.

If Qt cannot find its platform plugin, preserve qt.conf and the directory layout;
clear a stale external QT_PLUGIN_PATH/QML_IMPORT_PATH before retrying. `ldd` on the
viewer with LD_LIBRARY_PATH pointing to this package's qt/lib can identify missing
system libraries. If the decoder is missing, restore the complete archive; originals
are not repaired or overwritten. Rename rather than delete damaged preferences, or
use the viewer's existing --reset-settings option if needed.

provenance.json inventories package files; the external archive JSON sidecar hashes
the completed archive and internal manifest. These hashes establish integrity, not
a digital signature. Qualification results accompany the artifact separately and
are not part of the immutable package inventory. The independent smoke verifies
generated-image decode, real screenshot source colors, package-local loaded Qt
modules, missing-decoder diagnosis and unchanged source/package hashes.

Native Wayland plugins are not in this pinned toolkit. A headless software-GL check
does not qualify native Wayland, another distribution, hardware acceleration,
physical DPI/refresh, ICC/HDR, or HDD/SMB behavior. Those rows remain unverified.
