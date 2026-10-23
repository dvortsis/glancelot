# EN-09 review, round 3 (fresh context)

**Verdict: APPROVED.** N-1 is resolved, and so are N-2 to N-5. Nothing is blocking. The new findings R3-1 to R3-6 are non-blocking. R3-1 (the ICC payload is copied verbatim) is a policy question for the lead. It is not a defect in the test plan.

- **Packet:** `origin/claude/en-09-feasibility`, round-2 diff `6f68aa5..fb90c18` (one commit, `fb90c18` "docs(en-09): review round 2"). Documents only.
- **Previous reviews:** `docs/verification/EN-09-REVIEW.md` (`c7bada9`) and `docs/verification/EN-09-REVIEW-R2.md` (`64a33b9`; N-1 blocking, N-2 to N-5 non-blocking).
- **Reviewer:** a fresh-context Claude Code Opus reviewer on the lead's Windows PC, 2026-10-22. It did not write EN-09, did not do round 1 or round 2, and did not make any of the fixes.
- **Not run:** no build, no ctest, no Docker. Nothing in the packet compiles.

## How this was checked

- Read `AGENTS.md`, the round-2 review, the full `6f68aa5..fb90c18` diff and the handoff section "Review round 2".
- **Qt writers:** downloaded the raw sources at tag `v6.8.3` and read the write paths directly:
  - qtbase: `src/plugins/imageformats/jpeg/qjpeghandler.cpp`, `src/gui/image/qpnghandler.cpp`, `src/gui/image/qimage.cpp` (`setColorSpace`), `src/gui/image/qimage_p.h` (colour-model compatibility), `src/gui/painting/qcolorspace.cpp`, `src/gui/painting/qicc.cpp`.
  - qtimageformats: `src/plugins/imageformats/webp/qwebphandler.cpp`, `src/plugins/imageformats/tiff/qtiffhandler.cpp`.
- **Libraries bundled in Qt 6.8.3:** the versions come from `qt_attribution.json` (libpng 1.6.47, libtiff 4.7.0, libwebp 1.5.0). The writer decisions that sit inside these libraries were read from the bundled sources:
  - libjpeg-turbo `jcparam.c` and `jcmarker.c`
  - libpng `pngwrite.c`, `pngset.c` and `pngwutil.c`
  - libtiff `tif_dirwrite.c` and `tif_dir.c`
  - libwebp `src/mux/muxedit.c`
- **libarchive:** downloaded `CMakeLists.txt` at v3.8.10 and listed every `OPTION`/`SET(... CACHE)` and every `FIND_*`.
- **zlib:** read the zlib.net home page, and `LICENSE` at tag v1.3.2.

## Status of round-2 findings

| Finding | Status | Evidence |
| --- | --- | --- |
| N-1 (blocking) deny-list test | **Resolved** | See below. F071 section 4 and section 5 now specify per-format allowlists that "fail on anything not in the allowlist". The forbidden items remain as named assertions. DEP-14, the handoff F071 row and finding 7 point to them. Every allowlisted item is something the 6.8.3 writer can emit for the v1 policy, and every block a correct D9a can produce is on the list. |
| N-2 DPI and colour tables | **Resolved** (see note R3-2) | Section 2 now says DPI is always written and that D9a sets it from the preset (default 72 dpi). Convert-first is chosen: an all-grey table becomes `Grayscale8`, any other becomes `RGB32`/`ARGB32`. The encode step asserts `colorCount() == 0`. This is correct: `convertToFormat` to these formats leaves no colour table, and the JPEG/PNG/TIFF palette branches (`qjpeghandler.cpp:537-546`, `qpnghandler.cpp:775-858`, `qtiffhandler.cpp:744-786`) become unreachable. A side benefit: setting the DPI explicitly also stops Qt's default dots-per-metre, which can come from the host's screen, from entering the file. |
| N-3 libarchive OFF list | **Resolved** (see note R3-4) | Every option name exists in v3.8.10 with the stated default: `ENABLE_MD` (226), `ENABLE_BSDXML` (236), `ENABLE_LIBGCC` (241) default ON; `ENABLE_MBEDTLS`/`ENABLE_NETTLE`/`ENABLE_LZO` default OFF (223, 224, 229); `ENABLE_TAR`/`ENABLE_CPIO`/`ENABLE_CAT`/`ENABLE_UNZIP`/`ENABLE_TEST`/`ENABLE_INSTALL` (245-258). `FIND_PACKAGE(ZLIB 1.2.1)` is at line 495. The `dumpbin /dependents` / `ldd` evidence requirement was added. |
| N-4 zlib on Windows | **Resolved** (see note R3-5) | zlib 1.3.2 exists, released 17 February 2026 (zlib.net; this release addresses the 7ASecurity audit findings). `LICENSE` at v1.3.2 is the standard zlib licence, "(C) 1995-2026 Jean-loup Gailly and Mark Adler", which is GPL-compatible. DEP-11, the summary row, the net-effect line, DEP-02, F039 sections 3 and 5 and the G1 row, and the handoff dependency line all now say that slice B brings two libraries on Windows. DEP-11 says how the bomb test reaches zlib. Rule 3 does name zlib as an Ubuntu-package exception, so using `zlib1g-dev` in Docker follows the rules. |
| N-5 list consistency | **Resolved** (see note R3-6) | DEP-14 now includes Photoshop and the other named TIFF tags and points to F071 section 5. The handoff row covers all four formats. The PNG fixture now uses two keys: a value under 40 characters (tEXt) and one of 40 or more (zTXt). This matches the `>= 40` rule at `qpnghandler.cpp` `set_text`. The fixtures now also include APP1 XMP, APP13 IPTC, a WebP with `EXIF`/`XMP `, a TIFF with ImageDescription, Make and Artist, and an `Indexed8` source. |

### N-1 in detail: each allowlist checked against the writer

**JPEG.** The F071 allowlist matches what the writer can emit.

| Allowlist item | Source evidence |
| --- | --- |
| APP0 JFIF for RGB and grayscale; APP14 Adobe for CMYK only | Qt sets `in_color_space` to `JCS_GRAYSCALE`, `JCS_RGB` or `JCS_CMYK` before `jpeg_set_defaults` (`qjpeghandler.cpp:535-562`). `jpeg_default_colorspace` maps RGB to YCbCr, gray to gray and CMYK to CMYK (`jcparam.c:286-318`). `jpeg_set_colorspace` turns JFIF on for GRAYSCALE and YCbCr, and turns JFIF off and Adobe on for CMYK (`jcparam.c:357-392`). Qt never touches `write_JFIF_header`. |
| JFIF length 16, no thumbnail | `jcmarker.c:372` writes length `2+4+1+2+1+2+2+1+1`. |
| APP2 ICC only for RGB or CMYK output | `write_icc_profile` is called only for `JCS_RGB`/`JCS_CMYK` (`qjpeghandler.cpp:589-590`). Grayscale output therefore never carries ICC, as the doc says. |
| SOF0 or SOF2 in practice; SOF1 harmless | `jpeg_set_quality(..., TRUE)` forces baseline tables (`qjpeghandler.cpp:585`). `write_frame_header` emits SOF2 for progressive Huffman, SOF0 for baseline and SOF1 otherwise (`jcmarker.c:540-551`). Allowing SOF1 is harmless. |
| No DRI, no RSTn | `write_scan_header` emits DRI only when `restart_interval` changes from 0 (`jcmarker.c:594`). Qt never sets it. |
| No APP1, COM or anything else | `set_text` writes COM only for keys in the text map, which is empty by construction. The `ImageTransformation` write option rotates pixels and writes no EXIF (`qjpeghandler.cpp:1117-1121`). No other marker writes exist. |

Order is SOI, then APP0/APP14 (at `jpeg_start_compress`), then the APP2 markers, then DQT, SOF, DHT and SOS. This matches the table.

**PNG.** The allowlist (IHDR, iCCP when colour is kept, pHYs, IDAT, IEND; everything else fails) matches what the writer can emit.

| Chunk | Source evidence |
| --- | --- |
| iCCP only; never sRGB, gAMA or cHRM | Qt calls `png_set_iCCP` when `iccProfile()` is non-empty (`qpnghandler.cpp:815-827`). In the bundled libpng 1.6.47, `png_set_iCCP` only stores the profile and sets `PNG_INFO_iCCP` (`pngset.c:864-912`). It does not derive sRGB, gAMA or cHRM from the profile, as the 1.6.0-1.6.44 colourspace code did. `png_write_info` writes sRGB, gAMA and cHRM only when their own valid bits are set (`pngwrite.c:203-216`). Only `png_set_sRGB`, `png_set_gAMA` and `png_set_cHRM` set those bits, and Qt never calls `png_set_sRGB` or `png_set_cHRM`. |
| gAMA only with a writer gamma | `png_set_gAMA` is called only when the writer gamma is non-zero and no ICC is written (`qpnghandler.cpp:830-832`). The doc correctly forbids gAMA. |
| PLTE/tRNS only for palette images | Palette images are excluded by convert-first. |
| oFFs only for a non-zero offset | The rebuilt image has a zero offset (`qpnghandler.cpp:890-892`). |
| pHYs whenever dots-per-metre is positive | `qpnghandler.cpp:897-901`. |
| gIFx/gIFg only from the animation setters | `qpnghandler.cpp:939-953`. |
| No tIME, eXIf, bKGD, sBIT, cICP, mDCV, cLLI or unknown chunks | libpng writes each of these only if its setter was called, and Qt calls none of them. |
| No "Software" text | Qt writes text only from `qt_getImageText`, which is empty here. |

**WebP.** The allowlist matches what the writer can emit.

| Case | Source evidence |
| --- | --- |
| No valid colour space | Qt writes the plain `WebPEncode` output (`qwebphandler.cpp:304-307`): `VP8 `, `VP8L`, or `VP8X`+`ALPH`+`VP8 ` for lossy output with alpha. |
| Valid colour space | Qt passes a hand-made VP8X (flags 0x20, plus 0x10 for alpha) and an ICCP chunk to the muxer (`qwebphandler.cpp:265-303`). `WebPMuxAssemble` then throws that VP8X away and rebuilds it in `CreateVP8XChunk` (`muxedit.c:469-545`). It sets ICCP, EXIF and XMP flags only when those chunks exist, sets the animation flag only for ANMF, and sets the alpha flag from the image. It emits VP8X, ICCP, ANIM, the image, EXIF, XMP and unknown chunks only when present (`muxedit.c:633-641`). Qt sets only ICCP. |

So `VP8X` flags limited to 0x20|0x10, `ICCP`, `ALPH` and `VP8 `/`VP8L` is exact.

**TIFF.** The allowlist matches what the writer can emit.

| Item | Source evidence |
| --- | --- |
| Tags Qt sets | `QTiffHandler::write` sets 256, 257, 284, 296/282/283, 274 (from `qt2Exif(transformation)`, 1 by default), 34675 (non-empty profile only), and per format 262, 259 (none or LZW), 258, 277, 339, 278, 338, 332, 320 (`qtiffhandler.cpp:658-954`). |
| Tags libtiff adds | `TIFFWriteDirectorySec` adds only 273/279 from the strip setup (`tif_dirwrite.c:563-939`). It writes no Software, DateTime or NewSubfileType unless they are set, and Predictor (317) is written only if set, which Qt never does. |
| Directory count | One directory (`"wB"` mode, one `TIFFClose`). |

The allowlist covers all of these. 254 and 317 are absent, which is harmless.

**Would a correct implementation pass?** Yes, provided the test reads each allowlist as "permitted" and not as "required". See R3-3.

## New findings (all non-blocking)

### R3-1 (non-blocking, lead decision) The kept ICC profile is the source's bytes, verbatim

`QIcc::fromIccProfile` caches the original bytes in the colour space (`qicc.cpp:2011`). `QColorSpace::iccProfile()` returns those cached bytes before it generates anything (`qcolorspace.cpp:1119-1128`). So "colour kept" copies the source profile unchanged into APP2, iCCP, ICCP or tag 34675. That includes the profile's text and device tags: `desc`, `cprt`, `dmnd`/`dmdd`, ICC v4.4 `meta`, and the header date and device fields.

The PNG iCCP keyword is `cs.description()` (`qpnghandler.cpp:817`), which is the source profile's `desc` text. Calibration tools often put a monitor name or a date there.

In addition, `fromIccProfile` keeps the bytes even for a profile Qt cannot parse (an invalid `QColorSpace`, `qcolorspace.cpp:1141-1148`). The JPEG, PNG and TIFF writers test only `iccProfile().isEmpty()`, not `isValid()`, so such a profile also passes through. WebP requires `isValid()`.

None of this breaks the allowlist, which permits ICC by design, and the lead's brief treats ICC as acceptable. But the UI statement "all other metadata is removed" is only true if the profile counts as colour data rather than metadata.

Two cheap options for D9a:

1. When the source is equivalent to a named space (sRGB, Display P3, Adobe RGB, ProPhoto), tag the output with the named `QColorSpace`. Qt then regenerates a clean profile. `setDescription` also clears the cached bytes (`qcolorspace.cpp:1408-1412`).
2. Assert in the test that the emitted ICC bytes equal `outputColorSpace.iccProfile()`, so the profile that is written is the one D9a chose.

### R3-2 (non-blocking) Convert-first to `Grayscale8` interacts with "colour kept"

An all-grey palette becomes `Grayscale8`, and the effect depends on the output format:

- **JPEG:** the output has no ICC, because the writer skips APP2 for grayscale (line 589).
- **PNG:** the output gets an RGB `iCCP` on a grey PNG, which the PNG specification does not allow. `setColorSpace` accepts an RGB space on Gray data (`qimage_p.h`, `qt_compatibleColorModelSource`), and libpng 1.6.47's `png_set_iCCP`/`png_write_iCCP` do not check the profile's colour space against the colour type.

There is no leak in either case, but the output is malformed or loses its profile. Either convert to `RGB32` when colour is kept and the profile is RGB, or state that grey-palette output drops the profile. The test should expect whichever rule is chosen.

### R3-3 (non-blocking) Write the allowlists as "permitted", never as "required in this form"

A correct D9a would fail if the test hard-codes any of these:

- JFIF `density_unit == 1`. Qt chooses inches or centimetres by rounding error (`qjpeghandler.cpp:564-576`).
- TIFF ResolutionUnit = inch. Qt writes centimetres when dots-per-metre % 100 == 0 (`qtiffhandler.cpp:682-691`).
- A single IDAT.
- A VP8X chunk when no colour space is set. Qt then writes simple-format WebP.
- SampleFormat (339) on the 8-bit RGB TIFF path, where Qt does not set it.

One sentence in F071 section 5 when D9a is briefed would cover this.

### R3-4 (non-blocking) "Covers every external-library switch" omits `POSIX_REGEX_LIB`

`POSIX_REGEX_LIB` is a cache choice, not an `ENABLE_*` option (`CMakeLists.txt:265`, `"AUTO"`). On a libc without `regcomp` it can link libregex (lines 1301-1349). On glibc and MSVC it adds nothing in practice, and the `dumpbin`/`ldd` evidence would catch it. Optionally, the D-packet can pin `POSIX_REGEX_LIB` explicitly, after checking which values v3.8.10 accepts when the PCRE options are off.

### R3-5 (non-blocking) zlib wording

- **Signatures.** zlib.net publishes a `.asc` for each package. DEP-11 and source [35] can say "`.asc` published; key not yet confirmed" instead of "asset names not listable; signature unconfirmed".
- **"Ships one zlib, not two."** This is one version in up to two static copies: one inside `archive.dll` and one inside the decoder executable, if libjpeg-turbo links zlib there. This is fine across a DLL boundary. Note that `WITH_SYSTEM_ZLIB` only feeds libjpeg-turbo's PNG loader, which D7 never calls.
- **"libarchive does not export zlib symbols."** This holds for the Windows DLL. A Linux `.so` built from a static zlib with default visibility does export them. The bomb test linking zlib directly is correct either way.

### R3-6 (non-blocking) Small consistency nits

- DEP-14's inline summary says "SOFn", while F071 section 5 allows only SOF0/SOF1/SOF2 and forbids SOF3 and SOF5-SOF15. DEP-14 defers to F071, so this is cosmetic.
- F071 source [9] still cites the qtimageformats `6.8` branch, while [12] now cites `v6.8.3`. The WebP write path was checked here at `v6.8.3` and matches.

## What the lead should do with this

1. Treat EN-09 as review-approved. All of the F-1 and N-1 bar is met: the design closes the text leak, and the planned test fails closed on any block it does not know.
2. Decide R3-1, the verbatim source ICC: either accept it and word the UI as "colour profile kept", or normalise it. Carry that decision, R3-2 and R3-3 into the D9a/D8 packet brief.
3. Fold R3-4 and R3-5 into the F039 slice B (G1) brief. R3-6 is optional.
4. The go/no-go table from the first review stands.
