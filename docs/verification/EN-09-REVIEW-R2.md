# EN-09 review, round 2 (fresh context)

**Verdict: CHANGES REQUIRED** (one blocking item, N-1, which narrows F-1 to its test plan; the F-1 design itself is approved).

- Packet: `origin/claude/en-09-feasibility`, round-1 diff `a4085bc..6f68aa5` (`12d08d3` review round 1, `6f68aa5` streamed F039 bomb fixture). Documents only.
- First review: `docs/verification/EN-09-REVIEW.md` at `c7bada9` (F-1 blocking, F-2 to F-10 non-blocking).
- Reviewer: Claude Code Opus fresh-context reviewer on the lead's Windows PC, 2026-10-22. Did not write EN-09, did not do the first review, did not make the round-1 fixes.
- Not run: no build, no ctest, no Docker (a gate held this machine). Nothing in the packet compiles.

## How this was checked

- Read `AGENTS.md`, the first review, the full round-1 diff and the handoff "Review round 1" table.
- **Qt sources, downloaded raw at tag `v6.8.3`** (qtbase and qtimageformats) and read directly, not summarised: `src/plugins/imageformats/jpeg/qjpeghandler.cpp`, `src/gui/image/qpnghandler.cpp`, `src/gui/image/qimage.cpp`, `src/gui/image/qimagewriter.cpp`, `src/gui/image/qimagereader.cpp`, `src/plugins/imageformats/webp/qwebphandler.cpp`, `src/plugins/imageformats/tiff/qtiffhandler.cpp`.
- **Dependency build files, downloaded raw:** libjpeg-turbo `3.2.0` `CMakeLists.txt`, `ChangeLog.md`, `LICENSE.md`; libarchive `v3.8.10` `CMakeLists.txt`, `libarchive/CMakeLists.txt`, `COPYING`.
- **Web spot-checks (2026-10-22):** Exiv2 security advisories page, libheif releases page, Launchpad `ubuntu/noble/+source/libarchive`.
- Cross-document grep on the branch for stale wording ("EXIF removed", `libarchive13`, write API, D-hdrfile, `AllowOverride`, zlib).

## Status of round-1 findings

| Finding | Status | Evidence |
| --- | --- | --- |
| F-1 (blocking) Qt writers carry text | **Design resolved; test plan not fully resolved (see N-1)** | See below. |
| F-2 libjpeg-turbo libspng/zlib, `WITH_SIMD` | **Resolved** | `LICENSE.md` 3.2.0 lines 13-50: libspng mostly BSD-2-Clause, some libpng-2.0; SIMD code and zlib under Zlib. `CMakeLists.txt` 3.2.0: `WITH_TOOLS` "Disabling this disables WITH_TESTS" (288-296); `WITH_SYSTEM_SPNG`/`WITH_SYSTEM_ZLIB` default OFF (731-760); `TURBOJPEG_SOURCES` always lists `rdbmp.c`, `rdpng-*`, `rdppm-*`, `wrbmp.c`, `wrpng-*`, `wrppm-*` plus spng/zlib objects (774-783), so there is indeed no loader switch. `ChangeLog.md` 3.1.90 item 9: `tj3LoadImage*()`/`tj3SaveImage*()` support PNG. `WITH_SIMD`, `ENABLE_SHARED`, `ENABLE_STATIC` (default ON) exist. |
| F-3 XMP in the GUI process | **Resolved** | Rule 1 has no exemption; DEP-01 placement, DOCTYPE rejection, numeric caps and F4 target added; handoff finding 8 and question 6 aligned. |
| F-4 Exiv2 facts | **Resolved** | Advisories page: seven published 2026-08-30, one Moderate and six Low; four are `RemoteIo`/`http.cpp` (items 1-4), as the document now says. |
| F-5 libheif wording, x265 | **Resolved** | Releases page: 1.23.4 three High (3 Medium), 1.23.5 one High (3 Medium, 3 Low), 1.23.6 four High. x265 reworded as licence-compatible in DEP-05 and the DEP-16 traps row. |
| F-6 libultrahdr ISO 21496-1 | **Resolved** | F048 section 2, section 7 item 2 and source [5] now match DEP-12. |
| F-7 Radiance `.hdr` scope | **Resolved** | D-hdrfile is deferred with COLOR-07 in F048 sections 2, 4, 5, 6, 7; DEP-12 and rule 1 require worker placement and an F4 target. No stale "slice A includes D-hdrfile" text remains. |
| F-8 F039 versus DEP-11 | **Resolved**, with new gaps N-3 and N-4 | Launchpad lists binaries `libarchive13t64`, `libarchive-dev`, `libarchive-tools`, version `3.7.2-2ubuntu0.9`. F039 table and section 3 now build from source. libarchive `CMakeLists.txt` 627-659 and `libarchive/CMakeLists.txt` 236-239: with `ENABLE_LIBB2=OFF`, `ARCHIVE_BLAKE2` is set and `archive_blake2s_ref.c`/`archive_blake2sp_ref.c` are compiled; format sources are an unconditional list. `COPYING`: BLAKE2 files "triple-licensed ... CC0 1.0 Universal, OpenSSL or Apache 2.0"; CC0 election correct. Fixtures no longer use libarchive's write API; the `qCompress` stripping (4-byte length, 2-byte zlib header, 4-byte Adler-32) yields raw deflate, and `6f68aa5` streams the 1 GiB bomb instead of holding it. |
| F-9 brotli CVE | **Resolved** | Marker removed; CVE and GHSA cited; question 5 updated. |
| F-10 `-AllowOverride` rationale | **Resolved** | F074a row, F078 section 4 and the handoff F074 row attribute the narrowing to F076a/F078a. |

### F-1 in detail

Confirmed in the v6.8.3 sources, matching the round-1 text:

- JPEG read: `jpeg_save_markers(&info, JPEG_COM, 0xFFFF)` (`qjpeghandler.cpp:958`), COM parsed into key/value (975-991), `image->setText(...)` for each (1032-1033). JPEG write: `set_text` (465-477) emits one `JPEG_COM` per entry of `qt_getImageText`, truncated at `maxMarkerSize = 65533`; called at 588. ICC as APP2 (479-498), only for RGB/CMYK input (589-590).
- PNG read: `readPngTexts` (364-388) uses `png_get_text`, iTXt via `fromUtf8`; `setText` at 577. PNG write: `set_text` (668-724) writes iTXt when a character has `row() != 0` or is a control/C1 character, compressed when the value has 40 or more characters; called at 903. pHYs is written whenever dots-per-metre is positive (897-901), oFFs only for a non-zero offset (890-892), iCCP from the colour space (810-828).
- `qimage.cpp`: `copyMetadata(QImageData*, ...)` (1168-1175) copies `text`, offset, colour space and physical metadata, but not the colour table; the `QImage*` overload (1177-1185) copies text through `setText`. Call sites: `copy` (1239, 1325), `convertToFormat` helper (2218), palette conversion (2274), `mirrored` (3494), `rgbSwapped` (3648), `smoothScaled` (4708), `rotated90/180/270` (4717, 4747, 4761), `transformed` (4979), `colorTransformed` (5543). `setText` only inserts (4236-4244). `qt_getImageText` (6457-6466) merges the writer description with every image key whose **key** is non-empty, so `setText(key, QString())` still emits the key with an empty value, as the document says.
- `qimagewriter.cpp`: `setText` appends to the description (609-613), passed to the handler only if non-empty (670-671). Nothing else adds text.
- WebP (`qwebphandler.cpp`): ICCP is the only metadata read (135) and written (VP8X flags `0x20`, "Has ICCP chunk, no XMP, EXIF or animation", 271-295); no text either way.
- TIFF (`qtiffhandler.cpp`): no text, ImageDescription, XMP or EXIF handling; writes resolution (684-690), `TIFFTAG_ORIENTATION` from the writer transformation (698), `TIFFTAG_ICCPROFILE` (706), plus structural tags.

**Does "rebuild the image from pixels with no text" achieve it? Yes.** A `QImage` constructed over external data (`constBits()`, `bytesPerLine()`) gets a fresh `QImageData` with an empty text map; `copy()` on that wrapper then runs `copyMetadata` from the empty wrapper (1239), so the result has no text. Nothing downstream re-adds text unless `QImageWriter::setText`/`setDescription` is called, which the design forbids, and the design asserts `textKeys().isEmpty()` before `write`. `convertToFormat`/`copy`/`scaled` applied after the rebuild would also copy only the empty map. Neither the WebP nor the TIFF writer adds text or EXIF of its own. The "both encode inputs" note (re-decode path versus shared-memory pixels without a colour space) is correct.

So the leak is closed in the design: F071 headline, sections 1, 2, 4, 5, D9a, section 7, DEP-14 and handoff findings 5 and 7 all state it, and F046/D8 and F032 are named as inheriting the same helper. What remains is the test plan (N-1).

## New findings

### N-1 (blocking) F071 section 4 and section 5, DEP-14: the metadata test is a deny-list and does not detect every metadata container

The lead's bar for F-1 is that the planned test detects every metadata container in JPEG, PNG, WebP and TIFF output. The planned walkers list specific forbidden items, so anything not listed passes:

- **JPEG:** only APP1 and COM are forbidden. APP13 (IPTC / Photoshop IRB), APP3-APP15 in general (for example APP12 "Ducky", APP11 JUMBF/C2PA) and data after EOI would pass.
- **TIFF:** only 270, 700, 33723, 34377, 34665, 34853 are forbidden. Make (271), Model (272), Software (305), DateTime (306), Artist (315), HostComputer (316), Copyright (33432), DocumentName (269), ImageSourceData (37724), PrintIM (50341) and DNG private tags would pass; several of these identify a person or a camera.
- **PNG:** tEXt/zTXt/iTXt/eXIf are the main containers, but tIME and private ancillary chunks would pass.
- **WebP:** only `EXIF` and `XMP ` are forbidden; any unknown RIFF chunk would pass.

Qt 6.8.3 cannot write any of these today, so this is not a live leak; it is a regression guard that would not catch the next leak (a later EXIF writer in D8, a Qt upgrade, or qtimageformats acting on its own "### consider parsing EXIF and/or XMP" comment, `qwebphandler.cpp:142`).

Also, the JPEG parenthetical "(APP2 ICC only)" is wrong as an allowlist: `jpeg_set_defaults` enables the JFIF APP0 header for RGB and grayscale input and Qt never turns it off (no `write_JFIF_header` in `qjpeghandler.cpp`), so every Qt JPEG has APP0 JFIF with a density.

Change (F071 section 4 and section 5; DEP-14's test sentence; handoff F071 row):

1. Replace the deny-lists with **per-format allowlists**, failing on anything else:
   - JPEG, markers up to and including EOI: SOI, APP0 `JFIF` only, APP2 `ICC_PROFILE` only, APP14 `Adobe` only for CMYK output, DQT, DHT, DRI, SOFn, SOS and the entropy data with RSTn, EOI; no bytes after EOI.
   - PNG chunks: IHDR, PLTE, tRNS, iCCP, gAMA, cHRM, sRGB, pHYs, oFFs, IDAT, IEND.
   - WebP RIFF chunks: VP8X, VP8, VP8L, ALPH, ICCP (and ANIM/ANMF only if animation is ever exported).
   - TIFF IFD0 tags: the structural set (254, 256, 257, 258, 259, 262, 273, 277, 278, 279, 284, 317, 320, 338, 339, 332), resolution (282, 283, 296), ICC (34675) and Orientation (274) equal to 1; no SubIFDs and no second IFD.
2. Keep the deny-list items as named assertions, so a failure says *what* leaked.
3. Fix "(APP2 ICC only)" accordingly.

Once this is in, F-1 is fully resolved and the verdict becomes APPROVED; a check of the F071 section 4/5 and DEP-14 diff is enough.

### N-2 (non-blocking) F071 section 2 "How to clear": DPI and colour-table details

- "sets only the colour space and, if the preset says so, the DPI" does not match the writers. A `QImage` built over external data has the default dots-per-metre (non-zero), and the PNG writer writes pHYs whenever it is positive (`qpnghandler.cpp:897-901`) while the JPEG writer always fills JFIF density (564-576). The value comes from Qt's default, not the source, so it is harmless, but the text should say "DPI is always written; it is Qt's default unless the preset sets it", and the N-1 allowlist must accept pHYs and the JFIF density.
- The wrapper loses the colour table (`copyMetadata` does not copy it, line 1170, and the external-data constructor starts with none). For `Indexed8`/`Mono` images the rebuilt image would encode with no palette. Either convert to an RGB/RGBA format before the rebuild or copy the colour table explicitly; say which.

### N-3 (non-blocking) DEP-DECISIONS DEP-11: the "all OFF" list misses options that default ON

libarchive v3.8.10 `CMakeLists.txt` also defaults these to ON: `ENABLE_MD` (line 226, links libmd when found, which is common on Ubuntu), `ENABLE_BSDXML` (236) and `ENABLE_LIBGCC` (241). Add them to the OFF list. Name the tool and test switches instead of "tools and tests off": `ENABLE_TAR`, `ENABLE_CPIO`, `ENABLE_CAT`, `ENABLE_UNZIP`, `ENABLE_TEST` OFF (and `ENABLE_INSTALL` as the supply route needs). Also say the D-packet checks the configure summary and `dumpbin /dependents` / `ldd` output to show zlib is the only external library. (`ENABLE_MBEDTLS`, `ENABLE_NETTLE` and `ENABLE_LZO` default OFF, so their omission is fine.)

### N-4 (non-blocking) DEP-11 / DEP-16 / F039 section 3: zlib on Windows is an unnamed dependency

libarchive with `ENABLE_ZLIB=ON` must find a zlib. Ubuntu supplies one (rule 3 allows it in Docker), but Windows has none, and Qt's bundled zlib is private. No document names the zlib source, pin, licence notice or packaging for Windows; DEP-02 mentions "one pinned zlib" only in passing, and F039 section 5's bomb fixture streams through "the pinned zlib that libarchive already links", which the test can only call if zlib is exported or linked into the test too. Name it in DEP-11 (zlib 1.3.x or zlib-ng in compat mode, Zlib licence, fetched and hashed like the others per DEP-16, one copy shared with libjpeg-turbo via `WITH_SYSTEM_ZLIB=ON`), add it to the THIRD_PARTY_NOTICES impact, and state how the test binary reaches it. Without this, "only libarchive" understates F039 slice B's dependency cost on Windows.

### N-5 (non-blocking) Consistency of the metadata lists

- DEP-14's TIFF list ("ImageDescription, XMP, IPTC, EXIF or GPS tag") omits Photoshop (34377), which F071 section 4/5 include.
- The handoff F071 row lists "APP1/COM/tEXt/zTXt/iTXt and TIFF metadata tags" but not eXIf or the WebP chunks.
- F071 section 5 "a PNG with a short plain tEXt key and a long Latin-1 value (zTXt)" reads as one key; make it two keys (one value under 40 characters -> tEXt, one Latin-1 value of 40 or more -> zTXt).
- Optional, cheap: add a WebP source with `EXIF`/`XMP ` chunks and a TIFF source with ImageDescription, Make and Artist, so the tests also guard against Qt starting to read those containers.

Align all of these to the N-1 allowlist in one pass.

## Consistency across documents

Apart from N-5, `docs/F071-FEASIBILITY.md`, `docs/DEP-DECISIONS.md` (DEP-02, DEP-11, DEP-14, rule 1), `docs/F039-FEASIBILITY.md` (table, sections 2, 3, 5, sources 15-16) and `docs/handoff/EN-09-HANDOFF.md` (table, findings 5-8, questions, review round 1 section) agree. No remaining text says "EXIF removed" as the whole policy, recommends the apt libarchive packages, uses libarchive's write API for fixtures, keeps D-hdrfile in slice A, or ties `-AllowOverride` to resize.

## What the lead should do with this

1. Return N-1 (and, in the same pass, N-2 and N-5) to whoever holds EN-09: F071 sections 2, 4, 5; DEP-14; handoff F071 row. A diff check of those sections is enough to move this to APPROVED.
2. Fold N-3 and N-4 into DEP-11/F039 now or into the F039 slice B packet brief; neither changes a go/no-go.
3. The go/no-go table in the first review stands unchanged.
