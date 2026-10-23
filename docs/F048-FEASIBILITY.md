# F048 feasibility: HDR, high-bit-depth, gain-map and specialist channel inspection

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO for the SDR slices, DEFER native HDR output.** Headline: **GO** for (A) a high-precision working buffer with tone-mapped SDR presentation and truthful precision labels - 16/32-bit TIFF needs no new dependency at all - plus (B) channel isolation (ADV-03), and **DEFER** native HDR surface output, Ultra HDR gain maps, EXR and Apple Adaptive HDR HEIC until a physically qualified display exists. Size: A is L, B is M, HDR output is XL. Depends on: DECODE-TIER (D1), CAPABILITY (D2), RESOURCE-LEDGER (E1), EN-02 for `qtimageformats` in the container.

## 1. What the feature needs in this codebase

Inventory row F048: "HDR/high-bit-depth/gain-map and specialist channel inspection | E COLOR-04..07, FMT-07..10 / P3 / XL | Existing conditional requirements; calibrated output and format-specific evidence; SDR fallback and truthful precision labels."

Requirement rows (expansion milestone E3 unless noted):

- COLOR-04 "Integer/float input precision and native HDR output" - "M0 capability proof; E3 ... Source precision preserved, actual negotiated surface format documented; calibrated output measurements; SDR fallback and UI white correct."
- COLOR-05 "ISO 21496-1 gain maps and Ultra HDR JPEG" - "Independently labeled variants and conformance fixtures; gain/headroom sweeps and SDR base-layer fallback."
- COLOR-06 "Apple Adaptive HDR HEIC" - E3 **conditional**; "not inferred from ordinary HEIC support."
- COLOR-07 "EXR/Radiance SDR exposure/highlight compression preview" - "Float/negative/nonfinite sample policy, reversible tone map, channel selection limitations and numeric tests."
- FMT-07 ".exr, .hdr, Ultra HDR .jpg, gain-map .heic, 16/32-bit .tiff" - **"TIFF M5; others E3"**.
- FMT-08 `.svg`/`.svgz`, FMT-09 `.psd`/`.psb`/`.kra`/`.clip`, FMT-10 `.dds`/`.ktx2` - all E3, each a separate decode question; this document covers them only as "not part of the recommended slices".
- ADV-03 "RGB/alpha channel isolation, invert and luminance grayscale" - **M5**, not E3: "Reference pixel outputs; color-domain labels; alpha/opaque-image behavior and view-only persistence tested."

FMT-07 already separates TIFF (M5) from the rest (E3), and ADV-03 is M5. That is the split this document recommends.

### What the codebase already has, and the single line that blocks it

| Piece | State | Where |
| --- | --- | --- |
| A high-precision pixel format in the contract | **declared, unused**: `enum class PixelFormat { Rgba8, Rgba16Float }` | `include/glancelot/contracts.hpp` |
| A place to record what the display can do | `DisplayCapabilities{backend, devicePixelRatio, hdr, monitorProfileManaged}` | `include/glancelot/contracts.hpp` |
| A place to record what a decoder can do | `DecoderCapability{format, still, animated, multiPage, preview, highPrecision}` | `include/glancelot/contracts.hpp` |
| Per-screen colour reporting | `DisplayInfo{colorDepth = 24, colorSpace = "Unknown"}` | `include/glancelot/display_manager.hpp` |
| Monitor ICC application | 33^3 RGBA8 3D-LUT atlas, Windows `GetICMProfileW`, sRGB fallback elsewhere | `src/render/output_color.hpp` |
| Canvas | `QQuickRhiItem` uploading **`QRhiTexture::RGBA8` only**, rejecting anything that is not `QImage::Format_RGBA8888`, 256 MiB texture budget | `src/render/image_canvas.cpp:97,122` |
| Decoder worker | converts **every** source to 8-bit sRGB: `image.convertToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)` and fails with "Cannot convert source pixels to the SDR working format." | `src/worker/main.cpp:205-206, 252-253` |

So precision is destroyed in exactly two places in the worker and rejected in one place in the canvas. Everything else - the contract fields, the capability structs, the LUT path - is already shaped for this.

Two Qt facts make slice A much cheaper than it looks:

- **Qt 6.8's TIFF plugin already reads and writes high precision.** `qtiffhandler.cpp` maps TIFF `SAMPLEFORMAT_IEEEFP` and 16-bit samples to `QImage::Format_Grayscale16`, `Format_RGBX64`/`Format_RGBA64(_Premultiplied)`, `Format_RGBX16FPx4`/`Format_RGBA16FPx4(_Premultiplied)` and `Format_RGBX32FPx4`/`Format_RGBA32FPx4(_Premultiplied)`, and writes them back [1]. FMT-07's TIFF half therefore needs **no new dependency** - only permission to stop flattening to RGBA8.
- **`QQuickRhiItem` already supports a float colour buffer.** Its `TextureFormat` enum is `{RGBA8, RGBA16F, RGBA32F, RGB10A2}` with `setColorBufferFormat()` [2]. The canvas can hold and sample a 16-bit-float image today; what it cannot do is *present* beyond SDR, which is a swapchain question, not an item question.
- **`QColorSpace` in 6.8 can describe HDR transfer functions**: `TransferFunction` includes `St2084` (PQ) and `Hlg`, and `Primaries` includes `Bt2020` [3]. So PQ/HLG tagging and conversion are expressible without a third-party colour library.

New components slice A and B would add: a worker `--precision` decode path that keeps `RGBA16FPx4` (or `RGBA64`) and reports the source's real bit depth and sample format; a tone-map stage (`src/render/tone_map.hpp` or a shader variant) with a reversible exposure/highlight control as COLOR-07 demands; a channel-isolation shader variant for ADV-03; and capability text in D2's model instead of today's fixed SDR wording.

Owner: **Track D** (viewing, decode and colour). Slice A sits naturally after D1 (DECODE-TIER) and beside D4 (per-monitor ICC); slice B (ADV-03) is a small D6-adjacent packet. Native HDR output, if ever promoted, is a separate packet that cannot complete without the user's physical qualification.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance (latest release + date) | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| **Qt only** (TIFF 16/32-bit, QColorSpace PQ/HLG, QQuickRhiItem float buffer) | Qt LGPL-3 (dynamic) | Yes | Qt 6.8.3, already pinned | nothing new; needs `qtimageformats`, already in the Windows dev tree | **blocked until EN-02** adds `qtimageformats` to the image |
| **Write our own Radiance `.hdr` reader** (RGBE + RLE, ~200 lines; deferred with COLOR-07, see section 6) | n/a | n/a | n/a | nothing | nothing |
| libultrahdr | Apache-2.0 **OR** MIT (dual) [4] | Yes | v2.0.2, 13 Aug [5] | CMake build, one small library | not in the image today |
| OpenEXR | BSD-3-Clause [6] | Yes | v3.5.2, 03 Oct [7] | needs Imath, zlib and optionally libdeflate; several MiB | not in the image today |
| Imath (OpenEXR dependency) | BSD-3-Clause [8] | Yes | v3.2.3, 20 Aug [9] | header-heavy, small | same |
| libheif (for COLOR-06 Adaptive HDR HEIC) | LGPL for the library, MIT for samples and wrappers [10] | Yes (LGPL, dynamic) | v1.23.6, 05 Oct [11] | needs a HEVC decoder plugin; patent questions are a separate matter for the lead | same |
| libavif / libjxl (HDR AVIF, JPEG XL) | libavif BSD-2-Clause [12]; libjxl BSD-3-Clause [13] | Yes | libavif v1.4.2, 26 May 2026 [14]; libjxl v0.12.0, 01 Jul [15] | each pulls encoders/decoders of its own | same |

libheif, libavif and libjxl belong to `docs/DEP-DECISIONS.md` and FMT-04/FMT-05, not to this document; they are listed so the lead can see F048's HDR variants overlap those decisions rather than being independent of them.

Judgements:

- **Slice A needs nothing.** TIFF 16/32-bit is Qt [1]; tone mapping and channel isolation are shader work.
- **Radiance `.hdr` is not in slice A.** FMT-07 says "TIFF M5; others E3", and COLOR-07 ("EXR/Radiance SDR exposure/highlight compression preview") is deferred below, so an in-house RGBE reader belongs with COLOR-07, not with the M5 slice. It is small (~200 lines), but it is a new parser of untrusted bytes: when promoted it runs inside `glancelot-decoder` (DEP-DECISIONS rule 1), never in the GUI process, and gets an F4 fuzz target like the OpenEXR reader would.
- **libultrahdr is the right library if COLOR-05 is ever promoted** - permissive dual licence, current release, and it is the reference implementation of the gain-map scheme. Its README describes storing an SDR base image with a gain map and metadata so legacy readers see SDR, which is exactly the fallback COLOR-05 requires. ISO 21496-1 support is **stated upstream**: the v1.1.0 release notes name the "ISO21496-1 standard", v2.0.0 adds HEIF/AVIF "with ISO 21496-1 gain map metadata", and v2.0.1 applies the ISO 21496-1 gain-map patch automatically [5]. Conformance to COLOR-05's own fixtures ("independently labeled variants and conformance fixtures") still has to be proven by test before any support claim.
- **OpenEXR is a real, justified dependency** for COLOR-07, but it is the heaviest item here and brings Imath. EXR's deep and multipart features are explicitly out of scope per FMT-07 ("EXR deep/multipart/channel limitations explicit"), so the cost buys a narrow slice.

## 3. Platform capability

This is the section that decides the recommendation. The evidence is in Qt's own sources.

**How an HDR surface is requested at all.** Qt Quick 6.8 chooses the swapchain format in `QSGRhiSupport::applySwapChainFormat()`: it reads the `QSG_RHI_HDR` environment variable, or, if that is empty, the window property `_qt_sg_hdr_format`, accepting `scrgb`/`extendedsrgblinear`, `hdr10` and `p3`. If `isFormatSupported()` says no, it logs "Request ignored, using SDR swapchain" and silently stays SDR [16]. So there is a supported route (a per-window property, no private API), and the SDR fallback COLOR-04 demands is built in - but it is an env/property-level switch, not a public API, and the negotiated format is only observable through `QSG_LOG_INFO` logging and `hdrInfo()`.

**Windows.** Qt Quick's default backend on Windows is D3D11 [17], and the app does not override it (no `setGraphicsApi` call anywhere in `src/`; `perf_trace` records the backend name). `QD3D11SwapChain::isFormatSupported()` returns true for `HDRExtendedSrgbLinear` or `HDR10` only when the DXGI output's colour space is `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`, obtained from `DXGI_OUTPUT_DESC1` [18]; D3D12 is identical [19]. `hdrInfo()` then returns min/max luminance in nits with `SceneReferred` behaviour and 1.0 = 80 nits [18]. In other words **Qt already does the `IDXGIOutput6::GetDesc1` detection for us** - we do not need to write DXGI code, we need to read `QRhiSwapChain::hdrInfo()` and surface it through D2's capability model.

**Native Linux.** Qt Quick's default backend everywhere but Windows and Apple is OpenGL [17], and `QGles2SwapChain::isFormatSupported()` is literally `return f == SDR;` [20]. So on the project's Linux configuration there is **no HDR presentation at all**, regardless of compositor. The Vulkan backend does query `vkGetPhysicalDeviceSurfaceFormatsKHR` for a matching HDR surface format [21], so HDR on Linux would require switching the backend to Vulkan *and* a driver/compositor that exposes an HDR surface format. Whether any X11 or Wayland stack in Qt 6.8 actually does is **unconfirmed** - I could not reach Qt's documentation or bug tracker from this session, and I will not assert it from memory. Treat native Linux HDR as unavailable until someone demonstrates otherwise on hardware.

**Docker/noVNC.** Never HDR, structurally. `docker/start.py` launches `Xtigervnc :0 -geometry WxH -depth 24`, the image sets `QT_QPA_PLATFORM=xcb` with `LIBGL_ALWAYS_SOFTWARE=1`, and the RFB/VNC pixel path is 8 bits per channel. Even the OpenGL backend question is moot at `-depth 24`. The container must therefore report "SDR output, 8-bit" through the capability model and must not offer an HDR toggle. Separately, the container lacks `qtimageformats` until EN-02, so even **slice A's** 16/32-bit TIFF reading is unavailable there today - the capability model has to say that too, rather than silently showing a flattened image.

**Consequence for truthful labelling (COLOR-04's "actual negotiated surface format documented"):** three independent facts must be shown, never conflated - source precision and sample format (from the decoder), working-buffer precision (RGBA8 vs RGBA16F), and negotiated output surface (SDR / scRGB / HDR10, with the nits range when known). `DisplayInfo::colorSpace` is `"Unknown"` today and `colorDepth` is hard-coded 24; both must become real values or stay honestly unknown.

## 4. Security model

- Untrusted input is the central risk and it does not change shape here: everything new is still parsed inside `glancelot-decoder`, a separate process, under the existing allowlist, `QImageReader::setAllocationLimit(256)`, dims <= 32768 and the 256 MiB decoded ceiling. A new format means a new parser behind the same boundary, never in the UI process.
- **Allocation changes meaningfully.** An `RGBA16FPx4` buffer is 2x and `RGBA32FPx4` 4x the bytes of RGBA8 for the same pixels, so the 256 MiB decode ceiling and the 256 MiB canvas texture budget both have to be expressed in bytes-after-conversion, not in pixels. A 64 MP 32-bit-float image is ~1 GiB and must be rejected or tiered down, not attempted. This is the strongest argument for landing D1 (DECODE-TIER) and E1 (RESOURCE-LEDGER) first.
- **Non-finite and negative samples.** EXR and Radiance can contain NaN, infinities and negative values; so can float TIFF. The policy must be explicit (clamp or substitute, flagged in the HUD) and tested numerically, because NaN propagating into a tone map produces black or garbage frames and into a histogram produces undefined sorting. COLOR-07 already requires this.
- EXR is a complex format with its own CVE history; if OpenEXR is ever adopted it should become a fuzz target in the Linux build (Track F4) on the same footing as the metadata and IPC targets. The same applies to the in-house Radiance RGBE reader when D-hdrfile is promoted: it runs in the decoder worker and gets its own F4 target (truncated scanlines, RLE runs past the line end, exponent extremes, oversized header dimensions).
- Gain-map images carry a second embedded image plus metadata: both must be bounded independently, and a hostile gain map must not be able to drive the tone map to produce a pathological allocation.
- No authorization boundary changes: nothing here is a remote feature. Viewing never writes original media; exposure and channel isolation are view-only state (ADV-03 says "view-only persistence tested").

## 5. Test strategy

Generated fixtures only:

- **16/32-bit TIFF**: write them in-test with `QImageWriter` from a `QImage` in `Format_RGBA64` and `Format_RGBA32FPx4` (the Qt TIFF plugin writes both [1]), then read back and assert exact round-trip of chosen sample values - that is the "reference pixel outputs" COLOR-04 and ADV-03 ask for, with no external tool and no committed binaries.
- **Radiance `.hdr` (only when deferred D-hdrfile is promoted)**: write a tiny RGBE file byte by byte in the test (both flat and RLE-encoded scanlines), including a scanline of zeros and one with an exponent at the extremes; assert decoded float values and a clean rejection of a truncated file; plus the F4 fuzz target (section 4).
- **Precision labels**: table-driven - 8-bit PNG, 16-bit TIFF, 32-bit float TIFF (and Radiance once D-hdrfile is promoted) - each asserting the reported source depth, the working-buffer format and the output surface independently. A test must fail if an 8-bit source is ever labelled high precision.
- **Tone map**: numeric tests that the mapping is monotonic, that it is reversible as COLOR-07 requires, that NaN/Inf/negative inputs follow the declared policy, and that exposure 0 with no highlight compression is an identity on an SDR source.
- **Channel isolation (ADV-03)**: reference pixel outputs for R/G/B/alpha isolation, invert and luminance grayscale on an opaque image and on one with alpha; assert the colour-domain label changes with the mode; assert the source file is untouched.
- **Failure and cancellation**: a float TIFF whose decoded size exceeds the ledger (rejected with a named reason, nothing admitted); `stop_token` cancel during a high-precision decode; worker killed mid-transfer; a stale generation result dropped rather than replacing a newer selection.
- **Capability honesty in the container**: a test (or at minimum a documented offscreen check) that the capability model reports SDR 8-bit and absent `qtimageformats` rather than silently degrading.

New test file `tests/precision_ui_tests.cpp` plus a lead-provided `cmake/PrecisionTests.cmake`; never `tests/grid_ui_tests.cpp`.

**Stays unverified / RUNBOOK, and cannot be anything else:** every physical claim. Whether an HDR10 or scRGB swapchain is actually negotiated on a real HDR monitor; whether UI white is correct on that surface (COLOR-04); calibrated luminance measurements; gain-map headroom sweeps on a real display (COLOR-05); Apple Adaptive HDR HEIC, which COLOR-06 explicitly says must not be inferred from ordinary HEIC support. These require the user's hardware and a RUNBOOK packet. No headless or offscreen result may be relabelled as physical qualification, and until such a RUNBOOK passes, the UI must not offer an HDR mode at all.

## 6. Effort estimate

| Packet | Content | Size | Track |
| --- | --- | --- | --- |
| **D-prec1** | Worker high-precision decode path: keep `RGBA64`/`RGBA16FPx4` where the source has it, report real source depth and sample format, byte-accurate admission; `PixelFormat::Rgba16Float` finally used; 16/32-bit TIFF (FMT-07's M5 half) | L | D |
| **D-prec2** | Canvas float working buffer (`QQuickRhiItem::setColorBufferFormat(RGBA16F)`), reversible exposure/highlight tone map, truthful precision labels through D2's capability model | L | D |
| **D-chan** | ADV-03 channel isolation, invert, luminance grayscale, colour-domain labels, view-only persistence | M | D |
| *deferred* D-hdrfile | Radiance `.hdr` in-house reader in the decoder worker (+ fixtures and an F4 fuzz target); COLOR-07 / FMT-07 E3 scope, promoted with D-exr | S | D |
| *deferred* D-exr | OpenEXR + Imath, COLOR-07 scope only, no deep/multipart | L | D |
| *deferred* D-gain | libultrahdr, COLOR-05, SDR base-layer fallback first | L | D |
| *deferred* D-hdrout | Negotiated HDR swapchain via `_qt_sg_hdr_format`, `hdrInfo()` reporting, UI-white correctness, SDR fallback, Windows-only; completion requires a user RUNBOOK | XL | D |

Prerequisites: D1 DECODE-TIER (so a float decode has a tier to live in and a fallback when it is too big), D2 CAPABILITY (so the labels have a home and the fixed SDR text goes away), E1 RESOURCE-LEDGER (byte-accurate admission for 2x/4x buffers), EN-02 (`qtimageformats` in the container, or slice A is Windows/Linux-dev only), and a DEP-DECISIONS entry before D-exr or D-gain.

## 7. Recommendation and what would change it

**GO for slice A (D-prec1 + D-prec2) and slice B (D-chan).** (Review round 1 moved D-hdrfile out of slice A: Radiance is E3 scope under FMT-07 and COLOR-07, so it is deferred with COLOR-07 below.) They satisfy the M5 parts of the matrix (FMT-07's TIFF, ADV-03), they remove the two worker lines and the one canvas line that currently destroy and reject precision, they need no new dependency, and they produce the honest labels the whole conditional family depends on. They are also the prerequisite for ever doing HDR properly: without a float working buffer there is nothing to send to an HDR surface.

**DEFER native HDR output (D-hdrout).** Not because Qt cannot do it - D3D11/D3D12 can negotiate scRGB or HDR10 when Windows reports a PQ output [18][19], and Qt Quick exposes the request through a window property with automatic SDR fallback [16] - but because COLOR-04 requires "calibrated output measurements" and correct UI white on a real display, and because the project's other two platforms cannot do it at all (OpenGL is SDR-only by construction [20]; the container runs an 8-bit VNC desktop). A feature that is Windows-only, hardware-gated and unverifiable in CI should not be promised in v1.

**DEFER COLOR-05 (Ultra HDR), COLOR-06 (Adaptive HDR HEIC) and COLOR-07 (EXR and Radiance).** COLOR-05 and COLOR-07's EXR half each need a new third-party library whose only consumer is a deferred feature; COLOR-07's Radiance half (D-hdrfile) needs no library but is E3 scope and a new untrusted-input parser, so it is promoted together with D-exr, in the worker and fuzzed; COLOR-06 is already marked conditional and cannot be inferred from HEIC support, which the project does not have either.

What would change this:

1. **The user qualifies an HDR display and accepts a Windows-only feature.** Then D-hdrout becomes GO, sequenced after slice A, with a RUNBOOK packet as its completion evidence.
2. **A gain-map corpus appears in the user's own library.** Then COLOR-05 moves ahead of COLOR-07: libultrahdr is permissive, current and small [4][5], and its SDR base-layer path gives value even with no HDR output - we would show the base image correctly and label the gain map as present but unapplied. Upstream states ISO 21496-1 support (v1.1.0, v2.0.0, v2.0.1 release notes [5]); before promising it, prove conformance against COLOR-05's labelled variants and fixtures.
3. **An EXR or Radiance corpus appears.** Then D-exr (and D-hdrfile) become GO as a narrow COLOR-07 slice: OpenEXR BSD-3 plus Imath BSD-3 [6][8], scanline images only, deep and multipart explicitly unsupported, non-finite policy tested, and the reader fuzzed.
4. **Linux HDR becomes real** (Vulkan backend plus a compositor exposing an HDR surface format [21]). That would make D-hdrout cross-platform and change the cost/benefit - but it needs demonstrating on hardware first, and the project would also have to switch its Linux backend from the Qt default [17].

If the lead wants the smallest possible step: **D-prec1 alone**. Keeping precision in the worker and reporting the real source depth, with the canvas still flattening for display, already converts "we silently throw your 16-bit TIFF away" into "we read it and tell you what we are showing you" - which is what the truthful-labelling criteria actually ask for.

## Sources

All accessed 2026-10-22.

1. Qt 6.8 TIFF handler - maps 16-bit and IEEE-float TIFF samples to `Format_Grayscale16`, `Format_RGBX64`/`RGBA64`, `Format_RGBX16FPx4`/`RGBA16FPx4`, `Format_RGBX32FPx4`/`RGBA32FPx4`, and writes them - https://raw.githubusercontent.com/qt/qtimageformats/6.8/src/plugins/imageformats/tiff/qtiffhandler.cpp
2. Qt 6.8 `QQuickRhiItem` - `enum class TextureFormat { RGBA8, RGBA16F, RGBA32F, RGB10A2 }` and `setColorBufferFormat()` - https://raw.githubusercontent.com/qt/qtdeclarative/6.8/src/quick/items/qquickrhiitem.h
3. Qt 6.8 `QColorSpace` - `TransferFunction` includes `St2084` and `Hlg`; `Primaries` includes `Bt2020` - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/painting/qcolorspace.h
4. libultrahdr - "distributed under the terms of both the MIT license and the Apache License (Version 2.0)" - https://github.com/google/libultrahdr
5. libultrahdr releases, newest v2.0.2 dated 13 Aug (the page prints no year); v1.1.0 notes name the "ISO21496-1 standard", v2.0.0 adds HEIF/AVIF "with ISO 21496-1 gain map metadata", v2.0.1 "automatically apply the ISO 21496-1 gain map patch" (re-checked in review round 1) - https://github.com/google/libultrahdr/releases
6. OpenEXR - "OpenEXR is licensed under the BSD-3-Clause license" - https://github.com/AcademySoftwareFoundation/openexr
7. OpenEXR releases, newest v3.5.2 dated 03 Oct - https://github.com/AcademySoftwareFoundation/openexr/releases
8. Imath licence file (BSD-3-Clause) - https://raw.githubusercontent.com/AcademySoftwareFoundation/Imath/main/LICENSE.md
9. Imath releases, newest v3.2.3 dated 20 Aug - https://github.com/AcademySoftwareFoundation/Imath/releases
10. libheif `COPYING` - LGPL for the library, MIT for samples and wrappers - https://raw.githubusercontent.com/strukturag/libheif/master/COPYING
11. libheif releases, newest v1.23.6 dated 05 Oct - https://github.com/strukturag/libheif/releases
12. libavif licence file (BSD-2-Clause) - https://raw.githubusercontent.com/AOMediaCodec/libavif/main/LICENSE
13. libjxl licence file (BSD-3-Clause) - https://raw.githubusercontent.com/libjxl/libjxl/main/LICENSE
14. libavif releases, newest v1.4.2 dated 26 May 2026 - https://github.com/AOMediaCodec/libavif/releases
15. libjxl releases, newest v0.12.0 dated 01 Jul - https://github.com/libjxl/libjxl/releases
16. Qt Quick 6.8 `QSGRhiSupport::applySwapChainFormat()` - `QSG_RHI_HDR` env var or `_qt_sg_hdr_format` window property; `scrgb`/`extendedsrgblinear`, `hdr10`, `p3`; silent SDR fallback with a log message - https://raw.githubusercontent.com/qt/qtdeclarative/6.8/src/quick/scenegraph/qsgrhisupport.cpp
17. Same file - default RHI backend is D3D11 on Windows, Metal on Apple, OpenGLES2 otherwise - https://raw.githubusercontent.com/qt/qtdeclarative/6.8/src/quick/scenegraph/qsgrhisupport.cpp
18. Qt 6.8 D3D11 backend - `QD3D11SwapChain::isFormatSupported()` returns HDR formats only for `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` from `DXGI_OUTPUT_DESC1`; `hdrInfo()` reports min/max nits, `SceneReferred`, 1.0 = 80 nits - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/rhi/qrhid3d11.cpp
19. Qt 6.8 D3D12 backend - same check - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/rhi/qrhid3d12.cpp
20. Qt 6.8 OpenGL backend - `QGles2SwapChain::isFormatSupported()` is `return f == SDR;` - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/rhi/qrhigles2.cpp
21. Qt 6.8 Vulkan backend - queries `vkGetPhysicalDeviceSurfaceFormatsKHR` for a matching HDR surface format - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/rhi/qrhivulkan.cpp
