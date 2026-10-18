# Dependencies and artwork

Application source: GPL-3.0-or-later (LICENSE). Qt 6.8.3 shared libraries and runtime components: Qt project, available under the applicable LGPL-3.0/GPL licenses; per-component notices and included third-party licenses are recorded in the shipped Qt SPDX SBOMs. Source for the exact toolkit release is available at https://download.qt.io/archive/qt/6.8/6.8.3/single/ and https://code.qt.io/ . Qt license texts: https://www.qt.io/licensing/open-source-lgpl-obligations . Bundled library replacement is supported by dynamic linking.

Image decoding currently uses Qt's JPEG/PNG handlers and their included dependencies. Runtime packaging may include other Qt image handlers for UI resources; the viewer decode allowlist remains JPEG/PNG. Qt SBOMs record libjpeg-turbo, libpng, zlib and other transitive components and licensing. The release packaging copies those machine-readable manifests. No codec patent clearance is claimed for unimplemented formats.

Microsoft Visual C++ runtime files are distributed by Qt's deployment tooling under Microsoft's applicable redistributable terms; they are not application source. Build tools (CMake, Ninja, aqtinstall, MSVC) are not shipped as part of the viewer.

Slate & Ice icons, palette and mascot artwork were provided by the project owner in glancelot-slate-ice-assets-v1.zip. Original package README, asset manifest and checksums are retained in assets/brand. No external artwork-generation script is executed. The supplied package does not state a separate explicit redistribution license for artwork; public-release licensing of brand artwork remains a release prerequisite. Other theme palettes are locally generated project data, not imports of third-party named themes.

This is a local development package. Public-release source bundles, exact dependency notices and artwork licensing must be completed before external distribution.

The optional Docker browser image also contains Ubuntu-packaged TigerVNC, noVNC, websockify, Openbox, Mesa and tini. Distribution-provided package copyright/license notices remain under `/usr/share/doc` in that image; Qt manifests remain under `/opt/Qt/sbom`. These desktop-streaming components are not bundled into the Windows portable viewer.
