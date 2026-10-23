# EN-09 handoff: feasibility documents and dependency proposals

Track session S4 ("feasibility"), Phase 0 cycle 1. Written 2026-10-23.

- **Branch:** `claude/en-09-feasibility`
- **Base:** `origin/main` at `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`chore(lead): freeze Phase 0 cycle 1 contracts`)
- **Tip:** the commit that adds this handoff (parent `b1b83c3`); the exact tip SHA is in the session's final report and `git log -1 origin/claude/en-09-feasibility`.
- **Scope kept:** documents only. No source, CMake, queue, workflow, Dockerfile or `docker/` change.

## Files

| File | Content |
| --- | --- |
| `docs/F024-FEASIBILITY.md` | Visual similarity / near-duplicate groups |
| `docs/F039-FEASIBILITY.md` | Archive, comic and continuous-strip browsing |
| `docs/F048-FEASIBILITY.md` | HDR, high-bit-depth, gain-map and channel inspection |
| `docs/F071-FEASIBILITY.md` | Conversion, resize and export presets |
| `docs/F074-FEASIBILITY.md` | Remote desktop resize-to-browser |
| `docs/F076-FEASIBILITY.md` | Remote clipboard bridge and shortcut forwarding |
| `docs/F077-FEASIBILITY.md` | Browser upload/download staging area |
| `docs/F078-FEASIBILITY.md` | View-only / single-controller access and shared-session state |
| `docs/DEP-DECISIONS.md` | DEP-01..DEP-16 dependency proposals for Tracks C and D and the feasibility items |
| `docs/handoff/EN-09-HANDOFF.md` | This handoff |

## Recommendations (proposals; the lead records go/no-go)

| Item | Headline | GO slice (size, track) | Deferred / refused | Main condition that would change it |
| --- | --- | --- | --- | --- |
| F024 near-duplicates | **DEFER** | Optional early slice: hash core + worker `"hash"` mode, no UI | Whole feature until F023 (B7) and F028 (C5) integrate; embedding models no-go (download/size) | F023 and F028 integrated, then GO (XL), no new dependency |
| F039 archives / strip | **GO (two slices), DEFER rest** | A: continuous strip + spreads over folders, Qt only (L, Track A or new Track G). B: read-only ZIP/CBZ behind a new SOURCE-PROVIDER contract, libarchive in the decoder worker (XL) | RAR/CBR, 7z, tar, encrypted, solid, nested; UnRAR source permanently no-go | Lead freezes SOURCE-PROVIDER and accepts DEP-11; RAR/7z only after qualification |
| F048 HDR / precision | **GO (SDR slices), DEFER HDR output** | A: high-precision working buffer, tone-mapped SDR, truthful labels; 16/32-bit TIFF needs no new dependency (L, Track D). B: channel isolation ADV-03 (M) | Native HDR swapchain, Ultra HDR gain maps, EXR and Radiance `.hdr` (COLOR-07, E3; in-house reader in the worker with a fuzz target when promoted), Adaptive HDR HEIC | A physically qualified HDR display and RUNBOOK; Linux/Docker stay SDR (Qt's GL swapchain is SDR-only) |
| F071 export presets | **GO** | Qt-only JPEG/PNG (WebP/TIFF after EN-02) sibling of D8; "ICC kept, everything else removed" policy, enforced by writing a `QImage` rebuilt from pixels with no text map and tested at the byte level for APP1/COM/tEXt/zTXt/iTXt and TIFF metadata tags (L, Track D) | AVIF/JXL encoding; Lanczos3 until VIEW-05's resampler exists | AVIF/JXL encoders adopted for decode first (DEP-06/07) |
| F074 remote resize | **GO (hardening), DEFER resize** | F074a: `-AcceptSetDesktopSize=0` in `docker/start.py` (S, Track F, can join F1); the `-AllowOverride` narrowing is F076a/F078a's, not needed for resize | F074b resize-to-browser | Lead approves the RFB policy proxy; or Xvnc gains a size bound |
| F076 clipboard | **GO (policy slice), DEFER bridge** | F076a: explicit text-only clipboard through the noVNC panel, Xvnc limits, PRIMARY off, in-app routes for browser-reserved shortcuts, docs (M, Track F) | F076b per-client routing and browser system clipboard | RFB policy proxy; HTTPS made mandatory |
| F077 upload/download | **NO-GO for v1** (DEFER as separate project) | F077-0: document host file sharing into bind mounts (S, Track F) | Any in-container transfer service (XL) | Mandatory authenticated HTTPS front end, per-user credentials, F078 roles, F4 fuzzing and SYS-02 sandbox |
| F078 access modes | **GO (view-only password), DEFER rest** | F078a: second, server-enforced view-only VNC password (S-M, Track F) | F078b connection indicator from logs (M); F078c single-controller via proxy (L) | RFB policy proxy approved |

Dependencies (DEP-DECISIONS.md section 1): only **libjpeg-turbo 3.2.0** (D7) is proposed for Phase 2. XMP (C1) and hashing (B7) need no new library. libarchive arrives only with F039 slice B; libavif + dav1d and libjxl at M5 with FMT-04; LibRaw, libheif/libde265, OpenEXR/Imath and libultrahdr are deferred and pre-vetted.

## Findings the lead should see now

1. **Unbounded remote resize today (F074).** `docker/start.py` bounds `DESKTOP_SIZE` only at startup. Xvnc 1.13.1 accepts client SetDesktopSize by default up to 16384x16384 (about 1 GiB of framebuffer), and any authenticated noVNC user can switch on "Remote resizing". Xvnc has no option to cap requested sizes. F074a (`-AcceptSetDesktopSize=0`) is an S-size fix that also makes DOCKER.md's "resizing the browser does not change the server desktop" true unconditionally.
2. **Shared login lockout (F078).** Every client reaches Xvnc through websockify from 127.0.0.1, so failed-password back-off from one client blocks all clients.
3. **noVNC pin reason (F076, DEP-15).** Unreleased noVNC master reads the browser clipboard automatically in secure contexts. Do not move past 1.7.0 without review.
4. **One proxy, three slices.** F074b, F076b and F078c all need the same server-side RFB policy proxy (Python stdlib, between websockify and Xvnc). It is new security-sensitive protocol code; one decision covers all three.
5. **Codebase corrections to earlier assumptions.** Qt's JPEG/PNG/WebP writers keep ICC and drop EXIF, **but carry text chunks and comments unless they are cleared**: the JPEG and PNG readers turn COM markers and tEXt/zTXt/iTXt chunks (including XMP, which can hold GPS) into `QImage` text, `copyMetadata` keeps it through conversion, scaling and rotation, and the writers emit it again. Derived export must build the output from pixels with an empty text map (F071 section 2, DEP-14). Qt 6.8 already reads and writes 16-bit and float TIFF; the RGBA8 lines in `src/worker/main.cpp:205,252` and `src/render/image_canvas.cpp:97,122` are what block high precision (F048). Qt Quick 6.8 has an HDR swapchain request path on D3D11/D3D12; OpenGL (Linux, Docker) is SDR-only (F048). Qt's bundled libjpeg-turbo is a static internal archive and cannot be used for D7 (DEP-02). `QZipReader` is private and cannot bound decompression (F039).
6. **brotli security release** (2026-10-21, CVE-2026-111497, out-of-bounds write) matters for any future libjxl build: pin >= 1.2.1 (DEP-07).
7. **F046/D8 inherits the export metadata trap.** D8's privacy-aware export decodes and encodes through the same Qt paths, so it must use the same text-clearing step and the same byte-level test as F071 (one shared helper in the export worker family). The lead should carry this into the D8 packet brief.
8. **XMP sidecars belong in the decoder worker** (DEP-01, cross-cutting rule 1): read and token-preserving rewrite both parse untrusted bytes; the GUI process only writes the result. DOCTYPE is rejected outright and the size/depth/count caps are numbers C1 must test. Today's `metadata_parser.cpp` still runs in a GUI-process worker thread; whether to move it is a separate lead decision.

## Verification

- Documents only: no build or `ctest` was run, because nothing compiled changed. `git diff --stat afeac1a..HEAD` lists only the files above.
- Facts come from upstream sources fetched in the session (cited in each document). Hosts that were unreachable from the cloud session (libjpeg-turbo.org, libraw.org, aomedia.org, packages.ubuntu.com, MDN, doc.qt.io, libarchive.org, 7-zip.org and others) are named, and facts that depend only on them are marked **unconfirmed** in the documents.
- Release signatures could not be listed for most GitHub-hosted projects (API blocked); every "signed releases" claim that is not cited is marked unconfirmed.
- Nothing was run in Docker, a browser or on Windows. Every behaviour claim about Xvnc, noVNC, Openbox or Qt comes from source/man pages at the pinned versions, not from a test. Items listed as "unconfirmed" in F074 (Openbox refit on resize), F076 (per-browser cancellable shortcuts), F078 (Xvnc auth logging) and DEP-DECISIONS section 4 need a probe in the real image.
- ui-design-skills: not applicable (no UI produced). The GO slices with UI (F039 A, F048 B, F071, F076a shortcut affordances) must load them when implemented.
- Research was done through three parallel subagents (local-media features, remote features, dependency facts); the session reviewed and edited their output and wrote DEP-DECISIONS and this handoff.

## Questions for the lead

1. Record go/no-go per item (table above). Requirement-row amendments implied by the split recommendations (for example F048 SDR versus HDR, F039 slice A versus B) go through REQUIREMENTS.md change control.
2. Approve or refuse the RFB policy proxy (unblocks F074b, F076b, F078c).
3. Fold F074a (and F078a) into Track F's F1 packet now, or a separate small packet?
4. F039: freeze a SOURCE-PROVIDER contract (`AssetId` gains an `entry`), and which track owns it (new Track G, or slice A in Track A and slice B in Track D)?
5. DEP-16 supply route: FetchContent + SHA256 for libjpeg-turbo now and vcpkg at M5, or vcpkg from the start?
6. DEP-01: accept own XMP writer with infoset-level preservation as meeting META-05, run in the decoder worker?
7. DEP-05: HEIC deferred, or an opt-in build?
8. Qt stays at 6.8.3? (Only DEP-08, kimageformats, depends on it.)
9. Someone with an unrestricted network should confirm the unconfirmed provenance items in DEP-DECISIONS section 4 before the first pin.

## Review round 1

Review: `docs/verification/EN-09-REVIEW.md` on `origin/claude/review-en-09` (commit `c7bada9`), CHANGES REQUIRED (F-1 blocking, F-2 to F-10 non-blocking), against tip `a4085bc0d3ad0a5d2d419dab600f49abc5fd68dd`. Fixes made on 2026-10-22 by a different Claude Code session on the lead's Windows PC (the original cloud author was not reachable; this session is not the reviewer). Documents only: no source, CMake, workflow or Docker change; no build, `ctest` or Docker run, because nothing compiled changed and a full Windows gate held this machine. Upstream facts were re-checked on the web on 2026-10-22 and are cited in the documents.

| Finding | Change |
| --- | --- |
| F-1 (blocking) Qt writers carry text | Verified in Qt v6.8.3 `qjpeghandler.cpp` (COM saved, `setText`, `set_text` writes COM, truncated at 65533 bytes), `qpnghandler.cpp` (`png_get_text`/`setText`, `set_text` -> `png_set_text`, iTXt/compressed rules) and `qimage.cpp` (`copyMetadata` copies `text`; called by `convertToFormat`, `copy`, `transformed`/`scaled`, `smoothScaled`, mirror, rotations, `colorTransformed`; `setText` cannot remove a key; `qt_getImageText` merges description and image text), plus qtimageformats 6.8 TIFF (no text; writes ICC, Orientation, resolution) and WebP (no text/EXIF/XMP). F071 headline, section 1 (F046/D8 note), section 2 (new table columns, correction, "how to clear": rebuild the output `QImage` from pixels, set only colour space/DPI, assert empty `textKeys()`, never `QImageWriter::setText`/`setDescription`, both encode inputs), section 4, section 5 (metadata-carrying fixtures; structural JPEG/PNG/WebP/TIFF walkers asserting no APP1, COM, tEXt, zTXt, iTXt, eXIf, TIFF 270/700/33723/34377/34665/34853, Orientation absent or 1), D9a, section 7 and sources [7][8][9][11][12]. DEP-14 rewritten; handoff table row and finding 5 corrected; new finding 7 for F046/D8. |
| F-2 libjpeg-turbo TurboJPEG contains libspng + zlib | DEP-02 summary licence, licence paragraph (libspng BSD-2-Clause with libpng-2.0 portions, zlib under Zlib; no CMake switch drops the PNG/BMP/PPM loaders), new build-configuration bullet (`WITH_TOOLS=OFF`, `WITH_SIMD=OFF` option to keep NASM off the runner, never call `tj3LoadImage*`/`tj3SaveImage*`, record dead-stripping result, shared zlib with libarchive), notices now name libspng and zlib. Source [32]. |
| F-3 XMP in the GUI process | Cross-cutting rule 1 has no exemption; DEP-01 adds placement (read and rewrite in a decoder-worker mode, parent writes via temp + rename), DOCTYPE rejected outright, numeric caps for C1 (8 MiB, depth 64, 200,000 elements, 256 attributes, 1 MiB value, entity limit 0) and an F4 fuzz target. Summary row, handoff finding 8 and lead question 6 updated. |
| F-4 Exiv2 facts | Seven advisories on 2026-08-30 (one Moderate, six Low; four in `RemoteIo`/`http.cpp`), checked on the advisories page; expired tag key recorded as the review's observation; fallback must be built without WebReady/curl and the key checked against the attestation. |
| F-5 libheif wording and x265 | 1.23.4 three High, 1.23.5 one High, 1.23.6 four High (release notes checked). x265 stated as GPL-2.0-or-later and compatible; reason for `default-features: false` is unneeded encoder, patents and size (DEP-05 and DEP-16 traps row). |
| F-6 libultrahdr ISO 21496-1 | F048 section 2 and section 7 item 2 now say support is stated upstream (v1.1.0, v2.0.0, v2.0.1 notes) and conformance to COLOR-05 fixtures must still be proven; DEP-12 aligned; F048 source [5] updated. |
| F-7 Radiance `.hdr` scope | D-hdrfile moved out of slice A into the deferred COLOR-07 group (E3 under FMT-07); when promoted it runs in the decoder worker with an F4 fuzz target. F048 sections 2, 4, 5, 6, 7, DEP-12, rule 1 and the handoff F048 row updated. |
| F-8 F039 versus DEP-11 | F039 table and section 3 now follow DEP-11: build 3.8.10 from source in Docker and natively with the same minimal options, no apt package (noble name is `libarchive13t64`, confirmed on Launchpad). DEP-11: CMake has no per-format switch, so "ZIP only" = external libraries off except zlib plus run-time registration of the ZIP reader only; BLAKE2 files are compiled anyway, election recorded as CC0-1.0 (not OpenSSL); test fixtures are ZIP bytes generated in the test, not libarchive's write API; `.asc` assets and key fingerprint downgraded to "confirm before pinning". Sources F039 [15][16], DEP [33][34]. |
| F-9 brotli CVE | "verify" removed; CVE-2026-111497 and GHSA-9v8h-qqgm-4m35 cited from the release notes; brotli dropped from section 4 question 5; handoff finding 6 names the CVE. |
| F-10 `-AllowOverride` rationale | F074a row no longer claims the narrowing is needed for resize (`AcceptSetDesktopSize` is not in the 1.13.1 default list); narrowing attributed to F076a/F078a in F074, F078 section 4 and the handoff F074 row. |

The review's note that the original handoff could not name its own tip is answered above: the round-0 tip was `a4085bc0d3ad0a5d2d419dab600f49abc5fd68dd`. The round-1 tip is the commit that adds this section (`docs(en-09): review round 1`); its SHA is in the session report and `git log -1 origin/claude/en-09-feasibility`. Per the review, a check of the F071 diff (plus DEP-14) is enough to move the verdict to APPROVED.
