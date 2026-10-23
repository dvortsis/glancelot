# Third-party dependency decisions (proposals)

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22/23. **Every entry is a proposal. The lead decides; nothing is adopted until the lead records a decision here and a packet that uses it passes the authoritative gate.**

Scope: the libraries Tracks C and D need (XMP sidecars, lossless JPEG transforms, content hashing, optional RAW/HEIF/AVIF/JXL decode) plus what the eight feasibility documents added (archives for F039, HDR formats for F048, perceptual hashing for F024, export encoders for F071, the Docker desktop stack for F074-F078).

Starting point: Qt 6.8.3 is the only third-party library today (`CMakeLists.txt` finds only `Qt6` and `Threads`; no vcpkg, Conan or FetchContent). Windows ships a portable zip built by `scripts/build.ps1 -Package` (`cmake --install` + `qt_generate_deploy_qml_app_script`, Qt SBOM copied). Docker builds on Ubuntu 24.04 with Qt from aqtinstall and desktop components from apt. Glancelot is GPL-3.0-or-later; Qt is used under LGPL-3 with dynamic linking (THIRD_PARTY_NOTICES.md).

How facts were gathered: release tags fetched from the upstream git repositories, licence files read at the tag, Ubuntu versions from the noble / noble-updates / noble-security `Packages.gz` indexes, vcpkg versions from `microsoft/vcpkg` master, OSS-Fuzz from `google/oss-fuzz/projects`. Some hosts were unreachable from the cloud session (libjpeg-turbo.org, libraw.org, aomedia.org, packages.ubuntu.com among them); facts that depend only on those are marked **unconfirmed**. Dates are tag or commit dates and can differ from announcement dates by a day.

## 1. Summary

| ID | Need (consumer) | Proposal | Licence | Pin | New dependency? |
| --- | --- | --- | --- | --- | --- |
| DEP-01 | XMP sidecar read/write (C1, F025) | **Write our own** on `QXmlStreamReader`/`QXmlStreamWriter`, run in the decoder worker | ours | Qt 6.8.3 | No |
| DEP-02 | Lossless JPEG transforms (D7, F047) | **libjpeg-turbo** TurboJPEG `tj3Transform`, in a helper process | IJG AND BSD-3-Clause AND Zlib (TurboJPEG also contains libspng: BSD-2-Clause AND libpng-2.0) | 3.2.0 | Yes |
| DEP-03 | Content hashing (B7, F023) | **Qt `QCryptographicHash`** (BLAKE2b-256 or SHA-256) + byte compare | Qt LGPL-3 | Qt 6.8.3 | No |
| DEP-04 | Full RAW decode (E2, FMT-06) | **DEFER**; LibRaw is the pre-vetted candidate | LGPL-2.1 (chosen) or CDDL-1.0 | 0.22.2 | Later |
| DEP-05 | HEIF/HEIC (FMT-05, conditional) | **DEFER**; libheif + libde265 only as an opt-in build | LGPL-3.0-or-later | 1.23.6 / 1.1.3 | Later |
| DEP-06 | AVIF stills (FMT-04, M5) | **libavif + dav1d** (decode only, no libaom) when D-track starts FMT-04 | BSD-2-Clause | 1.4.2 / 1.5.4 | Yes, at M5 |
| DEP-07 | JPEG XL stills (FMT-04, M5) | **libjxl + highway + brotli (>= 1.2.1) + skcms** when FMT-04 starts | BSD-3-Clause (+ PATENTS) | 0.12.0 | Yes, at M5 |
| DEP-08 | How new codecs plug in | **Direct libraries behind small handlers in `glancelot-decoder`**, not KDE kimageformats | — | — | Policy |
| DEP-09 | WebP/TIFF/TGA in Docker (EN-02) | **qtimageformats from aqt**, same 6.8.3 as Qt | Qt LGPL-3 (bundles libwebp 1.5.0, libtiff 4.7.0) | 6.8.3 | Already used on Windows |
| DEP-10 | Colour management beyond `QColorSpace` | **Not now**; lcms2 only if a D4/F048 gap is shown | MIT | 2.19.1 | No |
| DEP-11 | ZIP/CBZ virtual source (F039 slice B) | **libarchive**, ZIP reader only, in the decoder worker, plus a pinned **zlib** (Windows has none); fallback miniz + own reader | BSD-2-Clause (+ noted exceptions); zlib: Zlib | 3.8.10; zlib 1.3.2 | Yes (two libraries on Windows), with F039 B |
| DEP-12 | HDR formats (F048 deferred slices) | **DEFER**: OpenEXR + Imath, libultrahdr pre-vetted | BSD-3-Clause; Apache-2.0 OR MIT | 3.5.2 / 3.2.3; 2.0.2 | Later |
| DEP-13 | Perceptual hashing (F024) | **Write our own** dHash + DCT pHash | ours | — | No |
| DEP-14 | Export encoders (F071) | **Qt `QImageWriter`** only (JPEG/PNG; WebP/TIFF via DEP-09) | Qt | 6.8.3 | No |
| DEP-15 | Docker desktop (F074-F078) | **Keep Ubuntu packages**; no new component; pin note on noVNC | GPL-2.0-or-later; MPL-2.0/BSD-2; LGPL-3.0 | noble versions | No |
| DEP-16 | Supply route for all of the above | **vcpkg manifest, pinned baseline, no default features, never Qt** (alternative: FetchContent + SHA256) | — | — | Policy |

Net effect for Tracks C and D in Phase 2: **one** new library (libjpeg-turbo, for D7). C1, B7, F024, F071 and the GO slices of F048 need none. libarchive (with a pinned zlib, which Windows needs) arrives only if the lead promotes F039 slice B. AVIF/JXL arrive with FMT-04 at M5.

## 2. Cross-cutting rules (proposed)

1. **Parsers live in a worker process.** Every codec, archive or metadata library that reads untrusted bytes links into `glancelot-decoder` (or a dedicated helper mode of it), never into the `glancelot` GUI process. This keeps SYS-02's parser boundary and the existing crash/deadline isolation (`src/worker/main.cpp`). The rule covers our own parsers of untrusted bytes too, with no exemption: XMP sidecars (DEP-01) arrive with the photos and are untrusted, so the XMP reader and the token-preserving rewriter run in a worker mode of `glancelot-decoder`; the GUI process only writes the result through temp sibling + rename. The same applies to an in-house Radiance `.hdr` reader if one is ever promoted (DEP-12). (Today's `metadata_parser.cpp` runs in a worker thread of the main process; moving it is outside EN-09 and is noted for the lead.)
2. **Qt-ABI rule.** Anything that is a Qt plugin or links Qt must be built against Glancelot's exact aqt Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`, `/opt/Qt/6.8.3/gcc_64`). Never take Qt, qtimageformats or Qt plugins from vcpkg (its Qt is 6.11.2) or from Ubuntu (Qt 6.4.2, KF5 plugins).
3. **Same versions on both platforms.** One pinned source per codec for Windows and Docker, so the two decode the same way and the capability page (D2) is one table. Ubuntu packages are acceptable only for desktop components (DEP-15) and small stable C libraries where a version difference cannot change output (zlib, expat), and then recorded as such.
4. **Dynamic linking for LGPL libraries** (LibRaw, libheif, libde265, Qt): DLL/.so beside the executable so users can replace them; corresponding source (URL + hash, plus any patches) listed in THIRD_PARTY_NOTICES. Static linking is fine for permissive libraries.
5. **Library limits on, environment off.** Use each library's own allocation and scan limits (TurboJPEG `TJPARAM_MAXMEMORY`/`MAXPIXELS`/`SCANLIMIT`, libheif `heif_security_limits`, LibRaw size limits, libarchive per-entry byte caps) in addition to the existing 32768-pixel and 256 MiB caps. The decoder clears library-controlled environment variables before loading codecs (for example `LIBHEIF_SECURITY_LIMITS`, `LIBHEIF_PLUGIN_PATH`) and disables plugin loading.
6. **Provenance per bump.** For each version bump, record here: upstream URL, version, SHA-256 of the source archive, and the result of the upstream signature check where one exists (GPG `.asc`/`.sig`, sigstore). vcpkg's SHA512 pins are maintainer review, not upstream signature verification.
7. **Notices.** Each adopted library adds a THIRD_PARTY_NOTICES paragraph and its licence files under `licenses/<name>/` in the Windows zip and `/opt/glancelot/licenses/` in the Docker image, plus `PATENTS` files where they exist (libjxl, libaom). vcpkg's per-port `vcpkg.spdx.json` can be copied next to Qt's `sbom/`. Keep the existing sentence "No codec patent clearance is claimed".
8. **Security watch.** Each adopted parser gets a fuzz target in F4 and a line in this document naming its advisory feed. A dependency with an open High advisory blocks a release (F6), not development.

## 3. Decisions

### DEP-01 XMP sidecar read/write (C1)

| Option | Licence / GPL-3 | Version, maintenance | Provenance | Fit for META-05 "preserved unknown XML" |
| --- | --- | --- | --- | --- |
| **Write our own** (Qt `QXmlStreamReader`/`Writer`) | ours | Qt 6.8.3 | n/a | **Only option that can keep unknown XML token-for-token**: stream with `writeCurrentToken()` and replace only the properties we own. `QXmlStreamReader` does not expand external entities and has `setEntityExpansionLimit` [1] |
| Exiv2 | GPL-2.0-or-later: compatible (combined work ships as GPL-3; Exiv2 source must be offered) [2] | 0.28.9, 2026-08-30; active; many GHSAs (seven published on 2026-08-30: one Moderate, six Low; four of them in `RemoteIo`/`http.cpp`, the WebReady/curl code); OSS-Fuzz yes [3] | GPG-signed tags, but the EN-09 review (2026-10-22) observed that GitHub shows the signing key as expired; release attestation JSON [3] | Rebuilds the packet from its data model on write (`XmpSidecar::writeMetadata` -> `XmpParser::encode`) [4]; keeps known/unknown *properties*, not comments or non-RDF XML. Pulls expat, zlib, brotli, inih. Ubuntu has only 0.27.6 (outside upstream support) |
| Adobe XMP-Toolkit-SDK | BSD-3-Clause: compatible [5] | v2025.03, 2025-03-28; low cadence; OSS-Fuzz no [5] | Unsigned tags | Same data-model rewrite; awkward build (expat/zlib dropped into `third-party/` by hand); no vcpkg port |
| Exempi | BSD-3-Clause | 2.6.x; small; Windows unconfirmed | unconfirmed | Wraps the Adobe SDK |

**Proposal: write our own.** Scope: a bounded RDF/XML subset reader (Description with attribute and element properties, Bag/Seq/Alt, `xml:lang`, `parseType="Resource"`, nested structs, qualifiers) and a token-preserving editor, roughly 2.3-3.7k lines (estimate) plus the Lightroom / Capture One / darktable fixture matrix C1 already requires.

- **Placement.** Sidecars are untrusted input (they arrive with the photos, and SYS-02 asks for parser boundaries), and the token-preserving rewrite re-parses the existing sidecar, so **both read and rewrite run in a worker mode of `glancelot-decoder`** (for example `--xmp-read` / `--xmp-rewrite`), not in a thread of the GUI process. The worker returns the parsed fields or the new sidecar bytes; the parent writes them through temp sibling + rename and the B2 journal as C1 specifies.
- **Hard rejections.** Reject any document containing a DOCTYPE outright (`QXmlStreamReader::DTD` token -> fail), rather than relying on `setEntityExpansionLimit(0)` alone; also reject processing instructions other than the `xpacket` wrapper, and any non-UTF-8 encoding declaration.
- **Numeric caps (proposals C1 must test at, just below and just above):** sidecar size 8 MiB; element nesting depth 64; total elements 200,000; attributes per element 256; any single attribute value or text node 1 MiB; entity expansion limit 0. Each limit fails with a named reason and leaves the sidecar untouched.
- **Fuzzing.** An F4 libFuzzer target for the reader and for read-then-rewrite (rewrite output must re-parse and must preserve every unknown token).

Caveat: output is infoset-identical, not byte-identical (`QXmlStreamWriter` normalises quoting and whitespace inside tags); C1's tests must state that. Notices: none. Change it if: C1 fixtures show the subset is too narrow for real sidecars, in which case Exiv2 0.28.x is the fallback (GPL-compatible, best maintained), accepting data-model-level preservation and documenting it against META-05. If Exiv2 is ever adopted: build it without WebReady/curl (`EXIV2_ENABLE_WEBREADY=OFF`, `EXIV2_ENABLE_CURL=OFF`), which the no-cloud rule requires anyway and which removes the code most of the 2026-08-30 advisories were in, and check the expired tag-signing key against the release attestation before pinning.

### DEP-02 Lossless JPEG transforms (D7)

- **Qt cannot be reused.** Qt 6.8.3 bundles libjpeg-turbo 3.1.0 as a static internal archive inside the qjpeg plugin, without `transupp.c`; it is not installed for linking [6].
- **libjpeg-turbo 3.2.0** (2026-06-30) [7]. Licence IJG (libjpeg API) + BSD-3-Clause (TurboJPEG API) + Zlib (SIMD code and the bundled zlib), all GPL-compatible; the IJG terms require the documentation statement "based in part on the work of the Independent JPEG Group" [8]. Since 3.1.90, `tj3LoadImage*()`/`tj3SaveImage*()` read and write PNG [7], so the TurboJPEG library now **contains libspng and zlib**: `LICENSE.md` lists libspng as mostly BSD-2-Clause with some code under the PNG Reference Library License v2, subsumed by the Modified BSD and IJG terms in the context of TurboJPEG, and zlib under the Zlib licence [8]. The bundled sources are built by default (`WITH_SYSTEM_SPNG`/`WITH_SYSTEM_ZLIB` are OFF), and 3.2.0 has **no CMake switch that leaves the PNG/BMP/PPM loaders out** of the turbojpeg target [32]. `tj3Transform` offers rotate/flip/transpose with `TJXOPT_PERFECT` (refuse partial iMCUs), `TJXOPT_TRIM`, `TJXOPT_CROP`, `TJXOPT_COPYNONE`, plus `TJPARAM_MAXMEMORY`, `MAXPIXELS`, `SCANLIMIT`, `SAVEMARKERS` [9]. OSS-Fuzz: yes. vcpkg 3.2.0. MSVC + CMake (NASM for SIMD).
- **Provenance.** Official tarball `libjpeg-turbo-3.2.0.tar.gz` with a detached GPG `.sig`; upstream says GitHub's auto-generated archives are not supported [7]. Signing-key fingerprint **unconfirmed** (libjpeg-turbo.org unreachable): check before pinning.
- **Docker.** Ubuntu noble ships 2.1.5 with only the 2.x `tjTransform` API; build 3.2.0 from source in the `build` stage (or via DEP-16) [10].
- **Gaps we must code.** `transupp.c` updates only ExifImageWidth/Height; the EXIF Orientation tag and the embedded EXIF thumbnail are **not** updated [11]. D7 must rewrite Orientation (to 1 after baking) and regenerate or drop the thumbnail, using our bounded EXIF code.
- **Placement.** Run transforms in a helper mode of `glancelot-decoder` (for example `--jpeg-transform`) that never loads the qjpeg plugin, to avoid any `jpeg_*` symbol interposition with Qt's static copy on Linux (risk **unconfirmed**; using only the `tj*` API also reduces it). Output goes to a temp sibling + rename after the B2 journal entry, as D7 already specifies.
- **Build configuration.** `WITH_TURBOJPEG=ON`, `ENABLE_SHARED=OFF`, `WITH_TOOLS=OFF` (which also turns tests off). D7 needs only coefficient-domain transforms, so `WITH_SIMD=OFF` is acceptable and keeps NASM off the Windows runner; measure transform time before deciding. **Unused attack surface:** D7 must never call `tj3LoadImage*()`/`tj3SaveImage*()` (the PNG/BMP/PPM file loaders), and the D7 packet records whether static linking with dead-code removal (`/OPT:REF`, `--gc-sections`) actually drops them; if it does not, the code is present but unreachable, and that is stated. If libarchive (DEP-11, which also needs zlib) later links into the same decoder binary, build both against the one pinned zlib 1.3.2 named in DEP-11 (`WITH_SYSTEM_ZLIB=ON` here) to avoid duplicate symbols.
- Alternative "write our own": coefficient-domain Huffman/arithmetic decode/re-encode with progressive and restart handling, about 5-8k lines; not advised.

**Proposal: adopt libjpeg-turbo 3.2.0 (static, TurboJPEG API only) for D7.** Notices: `LICENSE.md` and `README.ijg`, plus the IJG sentence in THIRD_PARTY_NOTICES, **and a named entry for the libspng and zlib code compiled into TurboJPEG** (libspng BSD-2-Clause with libpng-2.0 portions; zlib under the Zlib licence), with their licence texts under `licenses/libjpeg-turbo/`.

### DEP-03 Content hashing (B7, F023)

Qt 6.8.3 `QCryptographicHash` already provides SHA-256 (RFC 6234 C code), SHA-3 and BLAKE2b/BLAKE2s (reference C, since Qt 6.0) [12]. F023's pipeline is size grouping, then a head/tail partial hash, then a full hash, then a **byte-for-byte compare** as final validation; with that last step any hash is only a filter and a collision costs one extra compare.

- **Proposal: Qt BLAKE2b-256 (or SHA-256), no new dependency.** Hash in bounded chunks under `std::stop_token` and `IoScheduler` admission, never one `addData(QIODevice*)` call. Hard-link awareness uses `FileIdentity{volume,file}` (`include/glancelot/file_operations.hpp`) before reading.
- If profiling on NVMe shows hashing (not I/O) is the bottleneck: **xxHash** XXH3/XXH128 (BSD-2-Clause, v0.8.4, header-only, Ubuntu 0.8.2, vcpkg 0.8.4; unsigned releases) [13]. BLAKE3 (CC0/Apache-2.0, 1.8.7) is faster than BLAKE2 but adds assembly, has no Ubuntu library package and gains nothing once a byte compare follows [14].

### DEP-04 Full RAW decode (E2, FMT-06)

Glancelot already extracts embedded previews with its own bounded parser (`src/app/raw_preview.cpp`), which covers PERF-03's M3 "embedded-preview-first". Full sensor decode is E2.

- **LibRaw 0.22.2** (2026-07-15) [15]: dual LGPL-2.1 **or** CDDL-1.0; **choose LGPL-2.1** (CDDL is GPL-incompatible). Bundled demosaic code is BSD-3; the GPL "demosaic packs" were removed in 0.19 (2018) [16]. Snapshots every 9-18 months; 0.22 adds recent Canon/Nikon/Sony bodies and DNG 1.7. 0.22.1 fixed six Cisco Talos integer-overflow reports. OSS-Fuzz yes. Tags unsigned; official tarballs on libraw.org (**unconfirmed**). Ubuntu 0.21.2; vcpkg 0.22.2 (uses the separate LibRaw-cmake repo).
- **Proposal: DEFER until an E2 packet exists.** When adopted: dynamic, inside the decoder worker, OpenMP off, size limits set, LGPL-2.1 choice stated in notices. Alternative: none worth writing ourselves.

### DEP-05 HEIF / HEIC (FMT-05, conditional)

- **libheif 1.23.6** (2026-10-05), LGPL-3.0-or-later (examples MIT); **libde265 1.1.3** (2026-09-14), LGPL-3.0-or-later [17][18].
- **Risk signals.** The libheif README (October 2026) says one unfunded developer maintains both and that 73 security advisories were handled in 2026; 1.23.4 fixed three and 1.23.6 four High-severity advisories, and 1.23.5 fixed one High plus several Medium and Low; libde265 had two High and several Moderate advisories in 2026 [17][18]. Releases are unsigned tarballs.
- **Patents.** HEVC is covered by patent pools; no clearance is claimed or researched. KDE ships its HEIF plugin off by default.
- **Packaging traps.** vcpkg's `libheif` default features pull the x265 *encoder* and libde265: use `"default-features": false`. This is **not** a licence conflict: x265 is GPL-2.0-or-later and therefore compatible with a GPL-3.0-or-later work. The reasons to leave it out are that a viewer needs no HEVC encoder, that it adds HEVC patent exposure, and that it is large. libheif honours `LIBHEIF_SECURITY_LIMITS=off` and `LIBHEIF_PLUGIN_PATH`; the decoder must clear both and disable plugin loading.
- **Proposal: DEFER.** If the lead later wants HEIC: an opt-in build option, off in default packages, dynamic linking, decoder worker only, security limits on, and a capability-page line that says "conditional". Re-evaluate when the advisory rate drops or a second maintainer appears.

### DEP-06 AVIF stills (FMT-04)

- **libavif 1.4.2** (2026-05-26), BSD-2-Clause; OSS-Fuzz yes; unsigned artefacts [19].
- **dav1d 1.5.4** (2026-07-14), BSD-2-Clause, decoder only; Meson build (vcpkg handles it); Debian's upload carries an upstream `.asc` (signer **unconfirmed**) [20].
- libaom (BSD-2-Clause + AOM Patent License 1.0, which must be reproduced) is not needed for decode-only; skip it.
- Ubuntu noble: libavif 1.0.4, dav1d 1.4.1 (too old for parity) [10].
- **Proposal: adopt libavif + dav1d at M5 when Track D opens FMT-04**, static, decoder worker, libyuv optional. Gain maps (ISO 21496-1) stay with F048's deferred slice.

### DEP-07 JPEG XL stills (FMT-04)

- **libjxl 0.12.0** (2026-07-01), BSD-3-Clause since 0.5 (2021; earlier Apache-2.0) plus a Google `PATENTS` grant [21]. The 0.12 notes describe it as "for evaluation purposes" with extensive hardening; only two GHSAs (2021); OSS-Fuzz yes.
- Transitive: highway 1.4.0 (Apache-2.0 OR BSD-3-Clause), brotli (MIT), skcms (default; lcms2 optional). Release "Source code" archives lack submodules: fetch submodules at pinned commits.
- **brotli: pin >= 1.2.1.** Upstream released 1.2.1 / 1.1.1 / 1.0.10 on 2026-10-21; the notes state "CVE-2026-111497: out-of-bounds write permits remote code execution" (advisory GHSA-9v8h-qqgm-4m35, linked from the 1.1.1 and 1.0.10 notes); Ubuntu noble's index still showed 1.1.0, vcpkg had 1.2.0 [22].
- Ubuntu noble ships libjxl 0.7.0 (too old).
- **Proposal: adopt at M5 with FMT-04**, static, decoder worker, skcms. No region decode API was found in `decode.h` (**unconfirmed**), so D5 tiling falls back to scaled decode for JXL.

### DEP-08 How new codecs plug in: direct libraries versus KDE kimageformats

kimageformats (LGPL-2.0-or-later / BSD-2-Clause per file) would give AVIF, HEIF, JXL, EXR, RAW, PSD, QOI and more through `QImageReader`, which the decoder already uses, and its plugins respect `QImageReader::allocationLimit()` for output [23]. But:

- The last release that accepts Qt 6.8 is **v6.24.0 (2026-03-13)**; v6.25+ need Qt >= 6.9 and v6.31 needs Qt >= 6.10 [23]. With Qt pinned at 6.8.3 we would be frozen without upstream fixes.
- Its JXL plugin needs libjxl >= 0.9.4; it still needs every codec library underneath; it needs extra-cmake-modules and pkg-config on Windows.
- Ubuntu noble has only the KF5/Qt5 package; vcpkg has no port. We would build it from source against aqt Qt in both places.
- Its TGA plugin duplicates Qt's (Qt picks one arbitrarily).

**Proposal: direct libraries behind small Glancelot handlers in `glancelot-decoder`**, each with explicit limits, a capability row (D2) and a fuzz target. Revisit kimageformats only if the lead moves Qt to >= 6.10, where its maintained releases become usable.

### DEP-09 qtimageformats (EN-02)

Licence `LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only` (Qt); at 6.8.3 it bundles libwebp 1.5.0 and libtiff 4.7.0; `QTiffHandler` passes `QImageReader::allocationLimit()` to libtiff's cumulative allocation limit [24]. It reads and writes 16-bit and float TIFF (F048 slice A, F071). No ClipRect support in the TIFF/WebP handlers, so D5 tiled TIFF would need direct libtiff later.

**Proposal: install from aqt in the Docker `qt` stage and the hosted job (EN-02 already plans it); never from Ubuntu's `qt6-image-formats-plugins` (6.4.2).** Notices: covered by the Qt SBOM already copied.

### DEP-10 Colour management (D4, F048)

Qt 6.8 `QColorSpace` covers ICC v4 element lists (A2B/B2A), Gray/CMYK models, BT.2020, PQ (`St2084`) and HLG [25]. lcms2 2.19.1 (MIT, OSS-Fuzz yes, unsigned assets) adds rendering intents, black-point compensation and device links.

**Proposal: no lcms2 now.** Add it only if D4 or F048 shows a concrete gap; it is MIT and cheap to adopt then.

### DEP-11 Archives (F039 slice B)

- **libarchive 3.8.10** (2026-10-14): BSD-2-Clause with named file-level exceptions (3-clause UC Regents in the `compress` filter files, one public-domain file, BLAKE2 files CC0/OpenSSL/Apache-2.0) [26]. Release signatures: the release page lists 8 assets per release, consistent with three archives, three `.asc` signatures and two auto archives, but the asset names could not be listed; the `.asc` files and the signing-key fingerprint (C040B508D63D2B36 as recorded by the cloud session) are **unconfirmed: confirm both before pinning** [27]. OSS-Fuzz yes. Its RAR/RAR5 readers are its own BSD code, not RARLAB's (F039-FEASIBILITY.md).
- **Configure it small.** libarchive's CMake has no per-format switches: every format reader and writer source is compiled unconditionally [33]. So "ZIP only" is two things: (1) build-time, turn every external library off except zlib (`ENABLE_BZip2`, `ENABLE_LZMA`, `ENABLE_ZSTD`, `ENABLE_LZ4`, `ENABLE_LIBXML2`, `ENABLE_EXPAT`, `ENABLE_WIN32_XMLLITE`, `ENABLE_OPENSSL`, `ENABLE_CNG`, `ENABLE_LIBB2`, `ENABLE_ICONV`, `ENABLE_ACL`, `ENABLE_XATTR`, `ENABLE_PCREPOSIX`, `ENABLE_PCRE2POSIX`, `ENABLE_MD`, `ENABLE_BSDXML`, `ENABLE_LIBGCC` all OFF, which with the default-OFF `ENABLE_MBEDTLS`, `ENABLE_NETTLE` and `ENABLE_LZO` covers every external-library switch in v3.8.10 [33]; `ENABLE_ZLIB=ON` against the pinned zlib below; tools and tests off by name: `ENABLE_TAR`, `ENABLE_CPIO`, `ENABLE_CAT`, `ENABLE_UNZIP`, `ENABLE_TEST` OFF, `ENABLE_INSTALL` as the supply route needs. The packet that adds it shows the configure summary and `dumpbin /dependents` (Windows) / `ldd` (Docker) output proving zlib and the C runtime are the only libraries linked); (2) run-time, register only `archive_read_support_format_zip` and `archive_read_support_filter_none`, never `..._all`. Enable further formats only with qualification.
- **zlib, the second dependency slice B brings.** libarchive's `ENABLE_ZLIB` runs `FIND_PACKAGE(ZLIB 1.2.1)` [33]; Qt's bundled zlib is private, and Windows has no system zlib. So F039 slice B adopts **two** libraries on Windows, not one: **zlib 1.3.2** (2026-02-17, the newest upstream release [35]; Zlib licence, GPL-compatible), fetched as the official tarball with a SHA-256 pin per DEP-16 (upstream signature `.asc` and key **unconfirmed: confirm before pinning**), built as a static library and linked into `archive.dll`. The same zlib serves libjpeg-turbo (`WITH_SYSTEM_ZLIB=ON` pointing at it) if both are adopted, so the project ships one zlib, not two. The F039 bomb-fixture test links that same static zlib directly (a test-only link in the lead-provided test CMake), rather than expecting libarchive to export zlib symbols. Docker uses Ubuntu's `zlib1g-dev`, which rule 3 permits for zlib because inflate output cannot differ by version, or the same 1.3.2 source if the lead prefers strict parity. Notices: a THIRD_PARTY_NOTICES zlib entry and `licenses/zlib/` in the Windows zip; Docker keeps `/usr/share/doc/zlib1g/copyright`.
- **BLAKE2 licence election.** With `ENABLE_LIBB2=OFF`, libarchive compiles its bundled BLAKE2 reference code (`archive_blake2s_ref.c` and friends, used by RAR5) whether or not RAR5 is registered [33]. Record the election as **CC0-1.0** for those files (Apache-2.0 also acceptable; the OpenSSL-licence option is not GPL-compatible and must not be chosen), and name it in THIRD_PARTY_NOTICES.
- **Docker supply.** Ubuntu noble's runtime package is `libarchive13t64` 3.7.2 (not `libarchive13`), and it links acl, bz2, lz4, lzma, nettle, xml2 and zstd [10][34], so Docker builds 3.8.10 from source in the `build` stage with the same minimal options as Windows (rule 3), and the runtime stage copies the resulting library; neither `libarchive-dev` nor `libarchive13t64` is installed. F039 section 3 follows this.
- **Test fixtures.** Keep the shipped library read-only: tests generate ZIP bytes directly (stored entries plus raw-deflate entries made from `qCompress` output with the 4-byte length prefix, the 2-byte zlib header and the 4-byte Adler-32 trailer stripped, and a small CRC-32), so libarchive's write support is never relied on and the tested library is the shipped library (F039 section 5).
- Fallback: **miniz** 3.1.2 (MIT, single file) plus our own bounded central-directory reader (stored + deflate). minizip-ng (Zlib) is ZIP-only, its `MZ_FETCH_LIBS` defaults to ON on Windows and vcpkg lags (4.1.0); not preferred.
- Never: UnRAR source (use-restricted, GPL-incompatible); Qt's private `QZipReader` (private API, whole-entry inflate, no byte cap).

**Proposal: libarchive if and only if the lead promotes F039 slice B**; decoder worker only; per-entry byte cap and ratio limit; encrypted entries refused.

### DEP-12 HDR formats (F048 deferred slices)

- OpenEXR 3.5.2 (2026-10-03) + Imath 3.2.3: BSD-3-Clause; OpenEXR releases are signed with sigstore (`.sigstore.json`) [28]. About 78 published GHSAs including several High in September-October 2026; it may git-fetch OpenJPH unless forced internal-off.
- libultrahdr 2.0.2 (2026-08-13): Apache-2.0 OR MIT; the v1.1.0 notes already name the "ISO21496-1 standard", v2.0.0 extends it to HEIF/AVIF "with ISO 21496-1 gain map metadata", and v2.0.1 automatically applies the ISO 21496-1 gain-map patch. That is support **stated upstream**; conformance to COLOR-05's fixtures still has to be proven before any claim. No Ubuntu package or vcpkg port; its `UHDR_BUILD_DEPS` clones and patches libjpeg-turbo and libheif (avoid) [29].
- Radiance `.hdr`: small enough to write ourselves, but it is still a new parser of untrusted bytes and it is COLOR-07 / FMT-07 E3 scope, so it is deferred with COLOR-07 (F048 section 6). When promoted it runs in the decoder worker (rule 1) and gets an F4 fuzz target.

**Proposal: DEFER with F048's HDR slices.** Pre-vetted; adopt only with a physically qualified display RUNBOOK (gain maps, native HDR) or a qualifying corpus (EXR, Radiance).

### DEP-13 Perceptual hashing (F024)

pHash (GPL-3.0-or-later, compatible) has no upstream releases; OpenCV `img_hash` (Apache-2.0) lives in opencv_contrib and is a very large dependency. dHash and a DCT pHash over the existing 256-edge RGBA8 thumbnails are a few hundred lines.

**Proposal: write our own** (F024-FEASIBILITY.md), when F024 is promoted.

### DEP-14 Export encoders (F071)

Qt's writers keep ICC (JPEG APP2, PNG `iCCP`, WebP ICC chunk, TIFF ICC tag) and drop EXIF, **but they carry text unless it is cleared**: the JPEG and PNG readers turn COM markers and tEXt/zTXt/iTXt chunks (including an XMP packet that can hold GPS) into `QImage` text, `copyMetadata` keeps that text through conversion, scaling and rotation, and the JPEG and PNG writers emit it again as COM markers and text chunks (F071-FEASIBILITY.md section 2). Any export through `QImageWriter` (F071, F046/D8, F032) must therefore build the output `QImage` from pixels with an empty text map and never call `QImageWriter::setText`/`setDescription`, convert images with a colour table to a non-indexed format first, set the DPI explicitly, and its tests must check every output against a **per-format allowlist** of exactly the markers, chunks and tags the Qt 6.8.3 writer emits for that policy, failing on anything else (F071 section 5 has the lists: JPEG SOI/JFIF APP0/APP14 Adobe for CMYK/APP2 ICC/DQT/SOFn/DHT/SOS/EOI; PNG IHDR/iCCP/pHYs/IDAT/IEND; WebP VP8X/ICCP/ALPH/VP8/VP8L; TIFF structural, resolution, Orientation=1 and ICC tags). Named assertions on top say what leaked: APP1 EXIF/XMP, APP13 IPTC, other APPn, COM; tEXt, zTXt, iTXt, eXIf, tIME; WebP `EXIF`/`XMP `; TIFF ImageDescription, Make, Model, Software, DateTime, Artist, Copyright, XMP, IPTC, Photoshop, EXIF IFD, GPS IFD. JPEG and PNG come with qtbase; WebP and TIFF with DEP-09. AVIF/JXL encoding would pull libaom/SVT-AV1 or full libjxl encoders: deferred.

**Proposal: Qt only, no new dependency.**

### DEP-15 Docker desktop components (F074-F078)

| Component | Licence | In image (noble) | Upstream latest |
| --- | --- | --- | --- |
| TigerVNC Xvnc, `tigervncpasswd` | GPL-2.0-or-later | 1.13.1+dfsg-2build2 | 1.16.2 |
| noVNC | MPL-2.0 (core JS), BSD-2-Clause (HTML/CSS), CC BY-SA 3.0 images, OFL-1.1 font | 1:1.3.0-2 | 1.7.0 |
| websockify | LGPL-3.0 | 0.10.0+dfsg1-5build2 | 0.13.0 |
| Openbox | GPL-2.0-or-later (**unconfirmed**: check `/usr/share/doc/openbox/copyright`) | 3.6.1-12build5 | inactive |

These are separate programs in the container, so GPL-2.0-or-later and LGPL terms raise no combination question; notices stay under `/usr/share/doc` as THIRD_PARTY_NOTICES already says.

**Proposal: keep the Ubuntu packages; add nothing.** Record a pin reason: do not move to a noVNC newer than 1.7.0 without review, because unreleased noVNC master reads the browser clipboard automatically on focus in secure contexts, which would break F076's "no automatic harvesting" (F076-FEASIBILITY.md). The RFB policy proxy that F074b/F076b/F078c would need is our own Python stdlib code, not a dependency, and is a separate lead decision (EN-09 handoff).

### DEP-16 Supply route

| | (a) vcpkg manifest | (b) CMake FetchContent / ExternalProject | (c) Ubuntu apt |
| --- | --- | --- | --- |
| Pinning | `builtin-baseline` commit; per-port `overrides`; overlay ports when a port lags a security release | `URL` + `URL_HASH SHA256` | distro version, moves with updates |
| Integrity | port SHA512 pins; asset cache mirrors sources by hash | our hash; upstream signature checked by hand per bump | Ubuntu archive signature |
| Notices | per-port `copyright` and `vcpkg.spdx.json` SBOM | copied by hand | `/usr/share/doc/*/copyright` |
| Windows | native, `x64-windows` dynamic | each library's CMake (Meson for dav1d) is ours | n/a |
| Docker | `x64-linux` is static; use a dynamic triplet for LGPL libraries | same | old versions (libjpeg-turbo 2.1.5, libheif 1.17.6, libjxl 0.7.0, libavif 1.0.4, LibRaw 0.21.2, brotli 1.1.0) and Windows/Linux skew |
| Traps | never Qt; `default-features: false` (libheif pulls the x265 encoder: licence-compatible, but unneeded, patent-exposed and large); brotli port 1.2.0 < 1.2.1 | minizip-ng `MZ_FETCH_LIBS`, OpenEXR OpenJPH fetch, libultrahdr `UHDR_BUILD_DEPS`, libjxl submodules | Qt plugins never from apt |

**Proposal.** For the first dependency (libjpeg-turbo, D7) use (b): one lead-owned `cmake/ThirdParty.cmake` with `FetchContent_Declare(... URL ... URL_HASH SHA256=...)`, the official signed tarball verified once and the hash recorded here, built identically on Windows and in the Docker `build` stage. Switch to (a) vcpkg manifest mode, with a pinned baseline, binary cache and overlay ports, once three or more codec libraries are adopted (M5 FMT-04), because Meson builds and transitive trees (libjxl) make (b) expensive. Either way: never Qt, sources mirrored for offline rebuilds, and Docker gate time measured before and after (the build stage compiles everything).

## 4. Questions for the lead

1. Supply route (DEP-16): FetchContent now and vcpkg at M5, or vcpkg from the first dependency?
2. XMP (DEP-01): accept "write our own" with infoset-level (not byte-level) preservation as meeting META-05?
3. HEIC (DEP-05): defer, or opt-in build now? Patent posture stays "no clearance claimed" either way.
4. Qt version: staying on 6.8.3 rules out maintained kimageformats (DEP-08). Is a Qt move to >= 6.10 in scope for v1? It changes DEP-08 only.
5. Unconfirmed facts to check from an unrestricted network before pinning: libjpeg-turbo tarball key fingerprint; whether aqt's Qt 6.8.3 binaries use the bundled libjpeg-turbo 3.1.0 (`grep -i jpeg .tools/Qt/6.8.3/msvc2022_64/sbom/*`) and enable `openssl_hash`; whether libturbojpeg exports only `tj*`; libaom's patent licence position; Openbox copyright line; libarchive `.asc` assets and signing-key fingerprint (DEP-11). (The aqt Qt 6.8.3 bundled libjpeg-turbo version, 3.1.0, was confirmed from the local `qtbase-6.8.3.spdx` by the EN-09 review; the brotli CVE id is confirmed in the release notes.)

## Sources

Accessed 2026-10-22/23.

1. Qt 6.8.3 `qxmlstream.h/.cpp`: https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/serialization/qxmlstream.cpp
2. Exiv2 COPYING and README: https://github.com/Exiv2/exiv2/blob/main/COPYING , https://github.com/Exiv2/exiv2/blob/main/README.md
3. Exiv2 releases, security policy and advisories: https://github.com/Exiv2/exiv2/releases/tag/v0.28.9 , https://github.com/Exiv2/exiv2/blob/main/SECURITY.md , https://github.com/Exiv2/exiv2/security/advisories
4. Exiv2 v0.28.9 `src/xmpsidecar.cpp`, `include/exiv2/xmp_exiv2.hpp`: https://github.com/Exiv2/exiv2/blob/v0.28.9/src/xmpsidecar.cpp
5. Adobe XMP-Toolkit-SDK licence and releases: https://github.com/adobe/XMP-Toolkit-SDK/blob/main/LICENSE , https://github.com/adobe/XMP-Toolkit-SDK/releases
6. Qt 6.8.3 bundled libjpeg: https://github.com/qt/qtbase/blob/v6.8.3/src/3rdparty/libjpeg/qt_attribution.json , https://github.com/qt/qtbase/blob/v6.8.3/src/3rdparty/libjpeg/CMakeLists.txt
7. libjpeg-turbo releases and 3.2.0 ChangeLog: https://github.com/libjpeg-turbo/libjpeg-turbo/releases , https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/ChangeLog.md
8. libjpeg-turbo licence: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md
9. TurboJPEG 3.2.0 API: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/src/turbojpeg.h
10. Ubuntu noble package indexes: http://archive.ubuntu.com/ubuntu/dists/noble/main/binary-amd64/Packages.gz (and universe, noble-updates, noble-security)
11. libjpeg-turbo 3.2.0 `transupp.c`: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/src/transupp.c
12. Qt 6.8.3 `qcryptographichash.h/.cpp`: https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/tools/qcryptographichash.cpp
13. xxHash: https://github.com/Cyan4973/xxHash/blob/dev/LICENSE , https://github.com/Cyan4973/xxHash/blob/dev/README.md
14. BLAKE3: https://github.com/BLAKE3-team/BLAKE3/blob/master/README.md
15. LibRaw releases: https://github.com/LibRaw/LibRaw/releases
16. LibRaw COPYRIGHT and Changelog: https://github.com/LibRaw/LibRaw/blob/master/COPYRIGHT , https://github.com/LibRaw/LibRaw/blob/master/Changelog.txt
17. libheif README, COPYING, releases: https://github.com/strukturag/libheif/blob/master/README.md , https://github.com/strukturag/libheif/releases
18. libde265 COPYING and advisories: https://github.com/strukturag/libde265/blob/master/COPYING , https://github.com/strukturag/libde265/security/advisories
19. libavif licence and releases: https://github.com/AOMediaCodec/libavif/blob/main/LICENSE , https://github.com/AOMediaCodec/libavif/releases
20. dav1d tags and Debian upload: https://github.com/videolan/dav1d , https://tracker.debian.org/news/1776422/accepted-dav1d-154-1-source-into-unstable/
21. libjxl licence, PATENTS, CHANGELOG, v0.12.0: https://github.com/libjxl/libjxl/blob/main/LICENSE , https://github.com/libjxl/libjxl/blob/main/PATENTS , https://github.com/libjxl/libjxl/releases/tag/v0.12.0
22. brotli releases: https://github.com/google/brotli/releases
23. kimageformats README, CMakeLists by tag, LICENSES: https://github.com/KDE/kimageformats/blob/master/README.md , https://github.com/KDE/kimageformats/blob/v6.24.0/CMakeLists.txt
24. qtimageformats 6.8.3 attributions and TIFF handler: https://github.com/qt/qtimageformats/blob/v6.8.3/src/3rdparty/libtiff/qt_attribution.json , https://github.com/qt/qtimageformats/blob/v6.8.3/src/plugins/imageformats/tiff/qtiffhandler.cpp
25. Qt 6.8.3 `qcolorspace.h`: https://github.com/qt/qtbase/blob/v6.8.3/src/gui/painting/qcolorspace.h
26. libarchive COPYING: https://github.com/libarchive/libarchive/blob/master/COPYING
27. libarchive v3.8.10 release: https://github.com/libarchive/libarchive/releases/tag/v3.8.10
28. OpenEXR v3.5.2 release and SECURITY.md: https://github.com/AcademySoftwareFoundation/openexr/releases/tag/v3.5.2 , https://github.com/AcademySoftwareFoundation/openexr/blob/main/SECURITY.md
29. libultrahdr licence, releases, README: https://github.com/google/libultrahdr/blob/main/LICENSE , https://github.com/google/libultrahdr/releases
30. vcpkg ports and baseline: https://github.com/microsoft/vcpkg/tree/master/ports , https://github.com/microsoft/vcpkg/blob/master/versions/baseline.json
31. OSS-Fuzz projects: https://github.com/google/oss-fuzz/tree/master/projects
32. libjpeg-turbo 3.2.0 `CMakeLists.txt` (options `WITH_TURBOJPEG`, `WITH_TOOLS`, `WITH_SIMD`, `WITH_SYSTEM_SPNG`, `WITH_SYSTEM_ZLIB`; bundled `src/spng` and zlib objects linked into turbojpeg; no switch for the image loaders) and 3.2.0 `ChangeLog.md` (3.1.90 item: `tj3LoadImage*()`/`tj3SaveImage*()` support PNG): https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/CMakeLists.txt , https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.2.0/ChangeLog.md
33. libarchive v3.8.10 `CMakeLists.txt` and `libarchive/CMakeLists.txt` (`ENABLE_*` options for external libraries; format sources compiled unconditionally; `ARCHIVE_BLAKE2` set when libb2 is not found): https://github.com/libarchive/libarchive/blob/v3.8.10/CMakeLists.txt , https://github.com/libarchive/libarchive/blob/v3.8.10/libarchive/CMakeLists.txt
34. Launchpad, libarchive source in Ubuntu noble (binaries `libarchive13t64`, `libarchive-dev`, `libarchive-tools`; 3.7.2-2ubuntu0.9): https://launchpad.net/ubuntu/noble/+source/libarchive
35. zlib releases, newest v1.3.2 dated 17 Feb 2026 (asset names not listable; signature unconfirmed): https://github.com/madler/zlib/releases , https://zlib.net/

Review round 1 (2026-10-22, lead's Windows PC) re-checked: Exiv2 advisories page, libheif release notes 1.23.4-1.23.6, libultrahdr release notes, brotli release notes, libjpeg-turbo 3.2.0 `LICENSE.md`/`CMakeLists.txt`/`ChangeLog.md`, libarchive v3.8.10 CMake files and Launchpad.
