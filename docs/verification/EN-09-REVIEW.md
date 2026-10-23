# EN-09 fresh-context review

**Verdict: CHANGES REQUIRED** (one blocking finding, F-1; everything else is non-blocking).

- Packet: `origin/claude/en-09-feasibility`, base `afeac1a`, tip `a4085bc` (three commits, ten documents, documents only).
- Reviewer: Claude Code Opus fresh-context reviewer on the lead's Windows PC, 2026-10-22. Did not write or implement any part of EN-09.
- Scope read: `AGENTS.md`; LEAD-PLAN-2026-10-22 section 3 EN-09 row and the Phase 2 packet table (B1, B2, B7, C1, C5, D1, D2, D4, D5, D7, D8, E1, E2, F1, F2, F4, F6); `docs/FEATURE-OPPORTUNITIES.md` rows F024, F039, F048, F071, F074, F076, F077, F078; the `docs/REQUIREMENTS.md` rows the documents cite (ADV-02/03/10/11, FMT-04..07/12, COLOR-04..07, VIEW-05, META-05, SYS-02, SYS-04, PERF-03, UX-14/15); `LICENSE` (GPL-3 text; the project declares GPL-3.0-or-later in THIRD_PARTY_NOTICES); `THIRD_PARTY_NOTICES.md`; all ten EN-09 documents.
- Not run: no build, no ctest, no Docker (a gate was running on this machine). Nothing in the packet compiles.

## How this was checked

- **Repository references.** Spot-checked on the branch and all present: `src/worker/main.cpp` (lines 161-163 one-path argv and `fs_watch::probe`; 176 embedded-JPEG `QBuffer`; 205-206 and 252-253 `convertToColorSpace(SRgb, Format_RGBA8888)` with the exact failure string), `src/render/image_canvas.cpp:97,122` (RGBA8 rejection and `QRhiTexture::RGBA8`), `app/main.cpp:301` (`--window-size` with 540/360 floor), `include/glancelot/contracts.hpp` (`AssetId{path}`, `PixelFormat{Rgba8, Rgba16Float}`, `DisplayCapabilities`, `DecoderCapability`), `file_operations.hpp` (`FileIdentity`, `MutationTarget`), `selection_export.hpp` (`ExportStatus`, `ExportSkip`), `io_scheduler.hpp` (`JobPriority`, `tryAdmit`), `directory_listing.hpp`, `navigation.hpp`, `thumbnail_service.hpp`, `copy_move_service.*`, `display_manager.hpp`, `output_color.hpp`, `raw_preview.cpp`, `metadata_parser.cpp`, `metadata_model.cpp`, `command_registry.cpp` (Ctrl+Shift+P, F11, Ctrl+=/-/0, Alt+Left, Ctrl+Shift+N, Ctrl+Shift+Delete all present), `viewer_controller.cpp` (`setText`, `setUrls`, `setImage` clipboard paths), `docker/start.py` (560x400..3840x2160 bound, `Xtigervnc ... -depth 24 ... -SecurityTypes VncAuth -AlwaysShared`, no `-AcceptSetDesktopSize`), `Dockerfile` (`cp vnc.html index.html`, apt list without any archive library or qtimageformats), `compose.yaml` (`/photos` `read_only: true`), root `CMakeLists.txt` (only `Qt6` and `Threads`; no vcpkg/FetchContent). Inventory and requirement quotes match the source rows verbatim.
- **Local facts the cloud session could not check.** `.tools/Qt/6.8.3/msvc2022_64/sbom/qtbase-6.8.3.spdx` names LibJPEG-turbo 3.1.0 (`pkg:github/libjpeg-turbo/libjpeg-turbo@3.1.0`), confirming DEP-02's bundled-version claim. `plugins/imageformats` contains `qtiff`, `qwebp`, `qtga` and friends, and the qtimageformats SBOM is present, confirming DEP-09 "already used on Windows".
- **Web spot-checks (2026-10-22).** libjpeg-turbo releases and 3.2.0 `LICENSE.md` and `turbojpeg.h`; LibRaw releases and `COPYRIGHT`; libheif releases and README; libarchive releases; Exiv2 releases and security advisories; libjxl, libavif, libultrahdr, brotli releases; dav1d tags; xxHash and BLAKE3 releases and licences; Qt 6.8 `qcryptographichash.h`; kimageformats `CMakeLists.txt` at v6.24.0 and v6.25.0; Qt 6.8 `qjpeghandler.cpp`, `qpnghandler.cpp`, `qimage.cpp`. Results are in the findings and in the fact table at the end.

## Findings

### F-1 (blocking) F071 section 2 "What Qt 6.8's writers actually do" and section 4/5; handoff finding 5: the "ICC kept, everything else removed" policy is not true of Qt's writers as proposed

The document's own PNG row notes that `png_set_text` writes `QImage` text keys, yet the recommended policy and the handoff say Qt "drops EXIF" and that the UI will state "EXIF, GPS, XMP and maker notes" are removed. Checked in Qt 6.8 sources:

- `qjpeghandler.cpp` read path saves `JPEG_COM` markers and calls `image->setText(...)` for each; the write path calls `jpeg_write_marker(cinfo, JPEG_COM, ...)` for the image's text (`qt_getImageText`).
- `qpnghandler.cpp` read path calls `png_get_text` (tEXt, zTXt and iTXt, UTF-8 for iTXt) and `setText`; the write path calls `png_set_text` with every text key, as iTXt when needed.
- `qimage.cpp` `convertToFormat` copies metadata including the text map (`copyMetadata`, `dst->text = src->text`). `transformed()` (and so `scaled`/`scaledToWidth`/`scaledToHeight`), `smoothScaled()`, the rotation helpers and the format-converting `colorTransformed()` also call `copyMetadata`; the worker's in-place `convertToColorSpace` keeps the image's own metadata. So the text survives the decode, colour-conversion and resize steps D9a would use, and is certain to survive an encode straight from the reader's `QImage`.

So a PNG exported by Lightroom or darktable, which carries XMP in the `XML:com.adobe.xmp` iTXt chunk (XMP can include GPS, creator and keywords), keeps that XMP when exported to PNG and gets it written as a (truncated) JPEG comment when exported to JPEG. Source JPEG comments also pass through. The section 5 test ("no APP1/EXIF and no GPS") would pass while the leak exists.

Change:
1. F071 section 2 table and policy: state that Qt copies `QImage::text()` (JPEG COM, PNG tEXt/zTXt/iTXt including XMP) from reader to writer; D9a must clear every text key (build the output `QImage` without text, or remove each key) before `QImageWriter::write`, and must not rely on `setText` defaults.
2. Section 5: assert the output has no APP1, no COM, no tEXt/zTXt/iTXt (and, for TIFF, no ImageDescription or XMP tag; check `qtiffhandler.cpp` for its text handling), using a generated source that carries a PNG iTXt XMP packet with a GPS property and a JPEG COM marker.
3. Handoff finding 5 and DEP-14: correct "drop EXIF" to "drop EXIF but carry text chunks/comments unless cleared". Note that D8/F046 (privacy-aware export) inherits the same trap and should get the same test.

### F-2 (non-blocking) DEP-DECISIONS DEP-02 licence column and notices: libjpeg-turbo 3.2.0 TurboJPEG now contains libspng and zlib

The 3.2.0 `LICENSE.md` lists libspng (most code BSD-2-Clause, some under the PNG Reference Library License v2) and zlib as components of the TurboJPEG API library (used for packed-pixel image I/O); their terms are stated as subsumed by the IJG and BSD-3-Clause terms inside TurboJPEG. The licence summary "IJG AND BSD-3-Clause (AND Zlib)" is acceptable, but the notices entry should name libspng and the bundled zlib, and the D7 packet should check whether the PNG/BMP loader code can be left out of the build (unused attack surface in a helper that reads untrusted JPEGs). Also worth recording: D7 needs only coefficient-domain transforms, so `WITH_SIMD=OFF` is an option that keeps NASM off the Windows runner.

### F-3 (non-blocking) DEP-DECISIONS DEP-01 and cross-cutting rule 1: XMP parsing in the GUI process

Rule 1 says every untrusted-byte parser lives in `glancelot-decoder`, then exempts XMP sidecars to "a worker thread of the main process". Sidecars are untrusted input (they arrive with the photos), and SYS-02 asks for parser boundaries. Recommend the worker (or at least make the exemption explicit as a lead decision with an F4 fuzz target). Also: reject any DOCTYPE outright rather than relying on `setEntityExpansionLimit(0)` alone, and list the input-size and depth caps as numbers C1 must test.

### F-4 (non-blocking) DEP-DECISIONS DEP-01 Exiv2 facts

The Exiv2 advisories page shows **seven** advisories on 2026-08-30 (not six); one Moderate, six Low, mostly in `RemoteIo`/`http.cpp`. The release tags are GPG-signed but GitHub shows the signing key as expired. If Exiv2 ever becomes the fallback, record that it must be built without WebReady/curl (also required by the no-cloud rule) and that the expired tag key must be checked against the release attestation.

### F-5 (non-blocking) DEP-DECISIONS DEP-05 libheif facts and the x265 "trap"

- "1.23.4-1.23.6 each fixed several High-severity advisories": 1.23.4 fixed three High, 1.23.6 four High, but 1.23.5 fixed one High. Change to "1.23.4 and 1.23.6 each fixed several High advisories, 1.23.5 one". The README's "73 security advisories ... in 2026" and single-maintainer statements are confirmed.
- x265 is GPL-2.0-or-later and therefore GPL-3.0-or-later compatible; the reason to keep `default-features: false` is that it is an unneeded encoder with HEVC patent exposure and size, not a licence conflict. Reword so a reader does not think vcpkg's default libheif is licence-incompatible.

### F-6 (non-blocking) F048 section 2 versus DEP-12: ISO 21496-1 in libultrahdr

F048 says the ISO 21496-1 claim for libultrahdr is "unconfirmed"; DEP-12 says v2.0.0 added ISO 21496-1 gain maps. The release notes confirm the standard is named (v1.1.0 lists "ISO21496-1 standard"; v2.0.0 adds HEIF/AVIF "with ISO 21496-1 gain map metadata"). Align F048 with DEP-12: support is stated upstream; conformance to COLOR-05's fixtures still has to be proven before any claim.

### F-7 (non-blocking) F048 section 6/7: Radiance `.hdr` (D-hdrfile) is E3 scope inside a GO slice

FMT-07 says "TIFF M5; others E3". D-hdrfile pulls `.hdr` forward into the M5-oriented GO slice and adds a new in-house parser of untrusted bytes. Either move D-hdrfile to the deferred COLOR-07 group, or keep it in slice A explicitly as an E3 pull-forward that runs in the decoder worker and gets an F4 fuzz target (the security section lists a fuzz target for OpenEXR but not for the in-house RGBE reader).

### F-8 (non-blocking) F039 section 2/3 versus DEP-11: Docker supply of libarchive

F039 proposes `libarchive-dev` / `libarchive13` from apt; DEP-11 and cross-cutting rule 3 say build from source with the minimal ZIP+deflate configuration because Ubuntu's package links acl, bz2, lz4, lzma, nettle, xml2 and zstd. The noble runtime package is also `libarchive13t64`, not `libarchive13`. Reconcile F039 to DEP-11. Also F039 section 5 builds test ZIPs with libarchive's write API while DEP-11 configures "ZIP reader only": say whether write support is enabled for tests only, or generate fixture ZIP bytes directly in the test. Record which option is taken for the BLAKE2 files (CC0 or Apache-2.0; the OpenSSL-licence option is not GPL-compatible), or whether those files are compiled at all with RAR5 disabled. The libarchive `.asc` release assets could not be listed from here (8 assets per release, consistent with 3 archives plus 3 signatures plus 2 auto archives); keep the key fingerprint as an item to confirm before pinning.

### F-9 (non-blocking) DEP-07 brotli CVE marker

The brotli 1.2.1 / 1.1.1 / 1.0.10 release notes state "CVE-2026-111497: out-of-bounds write permits remote code execution" verbatim (GHSA-9v8h-qqgm-4m35). The "verify" marker and section 4 question 5's "brotli CVE id" can be dropped. (The 1.2.0 entry dated 27 Oct is 2025, not a conflict.)

### F-10 (non-blocking) F074 section 6 and F078 section 4: `-AllowOverride` rationale

F076 quotes the 1.13.1 default `AllowOverride` list (`desktop,AcceptPointerEvents,SendCutText,AcceptCutText,SendPrimary,SetPrimary`); `AcceptSetDesktopSize` is not in it, so F074a's "narrow `-AllowOverride` so X clients cannot re-enable it" is not needed for resize. Narrowing is still right for F076a/F078a (pointer and clipboard flags). Reword F074a so the shared narrowing is attributed to F076a/F078a.

Note for the lead: the handoff cannot name its own tip SHA; it is `a4085bc0d3ad0a5d2d419dab600f49abc5fd68dd` (commits dated 2026-10-23 UTC, consistent with the handoff date).

## Required checks

| Check | Result |
| --- | --- |
| Licences and GPL-3.0-or-later reasoning against `LICENSE` | Correct. GPL-2.0-or-later (Exiv2, TigerVNC, Openbox), LGPL-2.1 (LibRaw, choosing LGPL over CDDL), LGPL-3.0 (libheif, libde265, websockify, Qt), Apache-2.0 (highway dual, libultrahdr dual, OpenCV) and BSD/MIT/Zlib are all compatible with a GPL-3.0-or-later combined work; CDDL and the UnRAR licence are correctly rejected; Docker desktop components are separate programs. See F-2, F-5, F-8 for wording and notices gaps. |
| Versions and maintenance | Spot-checked versions all match upstream (fact table). Exiv2 count and libheif "each" wording are off (F-4, F-5). Unreachable-host facts are honestly marked unconfirmed. |
| References to real files and contracts | All spot-checked paths, line numbers, types and requirement quotes exist and match on the branch. `index.html` is created by the Dockerfile `cp`, as F074 says. |
| Untrusted input, bounded decompression and allocation | Covered well: parsers in `glancelot-decoder`, per-entry actual-byte caps and ratio limits enforced on `archive_read_data_block` output (F039), byte-accurate admission for 2x/4x float buffers (F048), TurboJPEG `MAXMEMORY`/`MAXPIXELS`/`SCANLIMIT` (confirmed present in 3.2.0 `turbojpeg.h`), libheif env/plugin lock-down, hash/index memory bounds (F024). Gaps: F-3 (XMP in GUI process), F-7 (no fuzz target for `.hdr`). |
| Server-side authorization (F074/F076/F077/F078) | Correct throughout. F074a uses `-AcceptSetDesktopSize=0` (server-enforced); F076a uses Xvnc cut-text, primary and size flags plus `AllowOverride` narrowing; F078a uses TigerVNC's second (view-only) password, which Xvnc enforces per message; noVNC `view_only` and landing-page settings are explicitly called conveniences, not boundaries; F077 is NO-GO precisely because no adequate server-side authentication exists. The shared-127.0.0.1 blacklist lockout and the unbounded SetDesktopSize finding are real and well argued from the 1.13.1 sources. |
| Product rules (no cloud, telemetry, model download, remote inference, automatic updates, mandatory catalog) | Respected. Embedding models rejected (F024); hash cache optional and disposable; no transfer catalog (F077); libheif plugin loading disabled; build-time fetches only (DEP-16) with mirrored sources for offline rebuilds; no runtime network use is proposed. |
| Go/no-go/defer clarity with conditions | Clear for every item, each with "what would change it" conditions; the handoff table summarises them. |

## Recommendations and reviewer agreement

| Item | EN-09 recommendation | Reviewer |
| --- | --- | --- |
| F024 near-duplicates | DEFER until B7 (F023) and C5 (F028) integrate and a requirement row exists; optional early C6a (hash core + worker hash mode, no UI); embedding models no-go | **Agree.** Banded 4x16-bit index for Hamming <= 3 is correct by pigeonhole. |
| F039 slice A (strip + spreads, Qt only) | GO now, L, Track A | **Agree it is feasible with no dependency**; ADV-10/ADV-11 are E1, so timing is the lead's scheduling call, not a feasibility result. |
| F039 slice B (ZIP/CBZ via libarchive) | GO after SOURCE-PROVIDER freeze, XL | **Agree, conditional** on the lead freezing SOURCE-PROVIDER (`AssetId::entry`), accepting DEP-11, and F-8 reconciled. |
| F039 RAR/CBR, 7z, tar, encrypted, solid, nested | DEFER; UnRAR source permanent no-go | **Agree.** libarchive's RAR readers are its own BSD code; the blocker is random access and parser risk. |
| F048 slice A (precision buffer, tone-mapped SDR, 16/32-bit TIFF) | GO, L, Track D | **Agree**, after D1/D2/E1; see F-7 for D-hdrfile. |
| F048 slice B (ADV-03 channel isolation) | GO, M | **Agree.** |
| F048 native HDR output, Ultra HDR, EXR, Adaptive HDR HEIC | DEFER | **Agree.** OpenGL `isFormatSupported` is SDR-only and the container is 8-bit VNC; COLOR-04 needs physical qualification. |
| F071 export presets (Qt-only) | GO, L, sibling of D8 | **Agree only after F-1 is fixed**: the metadata policy must clear text keys and be tested for COM/tEXt/iTXt. AVIF/JXL encode and Lanczos3 DEFER: agree. |
| F074a fixed-size hardening | GO, S, Track F (can join F1) | **Agree.** Closes a real unbounded allocation path; see F-10 for wording. |
| F074b resize-to-browser | DEFER until RFB policy proxy | **Agree.** |
| F076a clipboard policy + shortcut guidance | GO, M, Track F | **Agree.** |
| F076b per-client routing / browser clipboard | DEFER; pin noVNC at or below 1.7.0 | **Agree.** |
| F077 upload/download | NO-GO for v1; F077-0 docs only | **Agree.** |
| F078a view-only password | GO, S-M, Track F | **Agree**, including rejecting a view password equal to the control password. |
| F078b/F078c indicator and floor control | DEFER (proxy) | **Agree.** One proxy decision covers F074b, F076b, F078c. |
| DEP-01 XMP: write our own on QXmlStream | Proposal | **Agree** (only option that preserves unknown XML); see F-3, F-4. |
| DEP-02 libjpeg-turbo 3.2.0 for D7 | Adopt, static, TurboJPEG only, helper process | **Agree**; see F-2. Orientation/thumbnail gap in `transupp.c` correctly identified. |
| DEP-03 hashing: Qt `QCryptographicHash` | No new dependency | **Agree.** |
| DEP-04 LibRaw | DEFER, LGPL-2.1 choice | **Agree.** 0.22.2 and the six Talos fixes in 0.22.1 confirmed. |
| DEP-05 libheif/libde265 | DEFER; opt-in only | **Agree**; see F-5. |
| DEP-06 libavif + dav1d | Adopt at M5 with FMT-04 | **Agree.** |
| DEP-07 libjxl + brotli >= 1.2.1 | Adopt at M5 with FMT-04 | **Agree**; see F-9. |
| DEP-08 direct libraries, not kimageformats | Policy | **Agree.** v6.24.0 requires Qt 6.8.0, v6.25.0 requires 6.9.0 (confirmed). |
| DEP-09 qtimageformats from aqt | Proposal | **Agree.** Present in the local Windows Qt tree with SBOM. |
| DEP-10 lcms2 | Not now | **Agree.** |
| DEP-11 libarchive | Only with F039 slice B | **Agree**; see F-8. |
| DEP-12 OpenEXR/Imath/libultrahdr | DEFER | **Agree**; see F-6. |
| DEP-13 perceptual hash: write our own | Proposal | **Agree.** |
| DEP-14 Qt `QImageWriter` only | Proposal | **Agree with F-1 correction.** |
| DEP-15 keep Ubuntu desktop packages; noVNC pin reason | Proposal | **Agree.** |
| DEP-16 FetchContent + SHA256 now, vcpkg at M5 | Proposal (lead question) | **Agree** as a reasonable default; lead decision. |

## Fact spot-check table

| Claim (document) | Upstream, as checked | Result |
| --- | --- | --- |
| libjpeg-turbo 3.2.0, 2026-06-30, detached `.sig`, auto archives unsupported (DEP-02) | 3.2.0 released 30 Jun; `libjpeg-turbo-3.2.0.tar.gz.sig` present; notes say auto "Source code" assets are not supported | Confirmed |
| TurboJPEG 3.2.0 `TJPARAM_MAXMEMORY/MAXPIXELS/SCANLIMIT/SAVEMARKERS`, `TJXOPT_PERFECT/TRIM/CROP/COPYNONE` (DEP-02) | All present in `src/turbojpeg.h` at 3.2.0 | Confirmed |
| libjpeg-turbo licence IJG + BSD-3 + Zlib, IJG sentence (DEP-02) | Confirmed, plus libspng (BSD-2 / libpng-2.0) and zlib inside TurboJPEG | Confirmed, incomplete (F-2) |
| Qt 6.8.3 bundles libjpeg-turbo 3.1.0 (DEP-02) | Local `qtbase-6.8.3.spdx` names 3.1.0 | Confirmed |
| LibRaw 0.22.2, LGPL-2.1 or CDDL-1.0, BSD-3 demosaic, Talos fixes in 0.22.1 (DEP-04) | 0.22.2 released 16 Jul (tag date within the stated one-day tolerance); COPYRIGHT LGPL-2.1 or CDDL-1.0; DCB/FBDD BSD-like 3-clause; six TALOS-2026 ids in 0.22.1 | Confirmed |
| libheif 1.23.6 (2026-10-05), single maintainer, 73 advisories in 2026 (DEP-05) | Confirmed; 1.23.5 had one High | Confirmed, wording (F-5) |
| libavif 1.4.2 (2026-05-26) (DEP-06) | Confirmed | Confirmed |
| dav1d 1.5.4 (2026-07-14) (DEP-06) | Tag 1.5.4, Jul 14, 2026 | Confirmed |
| libjxl 0.12.0 (2026-07-01), "for evaluation purposes" (DEP-07) | Confirmed verbatim | Confirmed |
| brotli 1.2.1/1.1.1/1.0.10 on 2026-10-21, CVE-2026-111497 (DEP-07) | Confirmed verbatim | Confirmed (F-9) |
| kimageformats v6.24.0 last for Qt 6.8 (DEP-08) | v6.24.0 `REQUIRED_QT_VERSION 6.8.0`; v6.25.0 `6.9.0` | Confirmed |
| libarchive 3.8.10 (14 Oct), signed tags (DEP-11) | Confirmed; asset names not listable | Confirmed; `.asc` unconfirmed |
| Exiv2 0.28.9 (2026-08-30), six GHSAs that day (DEP-01) | 0.28.9 on 30 Aug; seven advisories that day | Count wrong (F-4) |
| libultrahdr 2.0.2 (13 Aug), ISO 21496-1 since 2.0.0 (DEP-12) vs unconfirmed (F048) | v1.1.0 and v2.0.0 notes name ISO 21496-1 | DEP-12 right, F048 stale (F-6) |
| Qt 6.8 `QCryptographicHash` has `Sha256` and `Blake2b_256` (DEP-03) | Both in the `Algorithm` enum of `qcryptographichash.h` (6.8) | Confirmed |
| xxHash v0.8.4, BSD-2-Clause (DEP-03) | v0.8.4 is the newest release; `LICENSE` is BSD 2-Clause | Confirmed |
| BLAKE3 1.8.7, CC0/Apache-2.0 (DEP-03) | 1.8.7 newest (20 Aug); CC0 1.0, or Apache-2.0 / Apache-2.0 with LLVM exception | Confirmed |
| Qt JPEG/PNG writers keep ICC and drop EXIF (F071) | True, but text keys (COM, tEXt/iTXt incl. XMP) are read and written back | Incomplete (F-1) |

## What the lead should do with this

1. Return F-1 to the packet author (or fix in the lead session): F071 sections 2, 4, 5 and 7, DEP-14 and handoff finding 5. Once corrected, this review's verdict becomes APPROVED without a further full re-review; a check of the F071 diff is enough.
2. Fold F-2 to F-10 in the same pass where cheap; none of them changes a go/no-go.
3. Record go/no-go per item using the agreement table above.
