# F071 feasibility: lightweight image conversion, resize and export presets

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO** for a Qt-only JPEG/PNG/WebP(/TIFF) slice built as a sibling of the D8 export family, with an explicit "EXIF removed, ICC preserved" policy; **DEFER** AVIF and JPEG XL encoding, and **DEFER** high-quality Lanczos3 resampling until VIEW-05's resampler exists so both share one implementation. Size L. Depends on: D1 DECODE-TIER, D8 (F046/F032 export worker family), E1 RESOURCE-LEDGER; EN-02 for WebP in the container.

## 1. What the feature needs in this codebase

Inventory row F071: "Lightweight image conversion/resize/export presets | C / P2 / L | Qualified encoders/export service; preview dimensions, color/metadata/quality policy, output conflicts and cancellation; never default to overwriting originals."

Sibling rows it must share machinery with, not duplicate:

- F046 "Privacy-aware derived export with optional GPS/metadata stripping | N / P2 / L | Export worker, metadata schemas; clear preserved/removed fields, filename/output policy, verify exported bytes and retain originals."
- F032 "Contact sheet export with filenames and chosen metadata | N / P2 / L | Bounded export worker; preview pagination, output dimensions/quality and overwrite policy; export never alters source media."
- F070 "Selection manifest or CSV export | N / P2 / M" - already delivered as `selection_export`, and the pattern to copy.

There is **no requirement row** for F071 in `docs/REQUIREMENTS.md`; the nearest related row is VIEW-05 "Lanczos3, bicubic, Mitchell and area downsampling | M3 default; M5 alternatives". So F071 is an inventory proposal and the lead would add acceptance criteria through change control.

### What to reuse (this is most of the design)

| Need | What exists, exactly | Where |
| --- | --- | --- |
| Safe single-file write | worker-thread engine; writes **one** new file via **temporary sibling + rename**; never writes, renames or truncates an exported item or any other existing file unless `overwrite` is set, and **never a selected item**; a cancelled or failed export leaves no destination file and no temporary file | `include/glancelot/selection_export.hpp`, `src/core/selection_export.cpp` |
| Status vocabulary already fit for purpose | `ExportStatus{Written, Cancelled, DestinationExists, DestinationIsItem, InvalidRequest, TooMany, Failed}`; `ExportSkip{Missing, Changed, Duplicate, OutsideRoot, Unrepresentable}` | same |
| Staleness and cancellation | `generation` echoed in the result so the caller drops stale results; `std::stop_token`; `revalidate` re-snapshots each item and skips `Missing`/`Changed` | same |
| Queue, progress, pause, cancel for many items | `CopyMoveService`: non-blocking worker queue, bounded chunked I/O, byte and item progress, pause/resume/cancel, collision policies `Skip`/`Overwrite`/`RenameBoth` | `include/glancelot/copy_move_service.hpp`, `src/app/copy_move_service.cpp` |
| Identity and collision primitives | `FileIdentity{volume, file}`, `MutationTarget`, `MutationService::validateTargetName` | `include/glancelot/file_operations.hpp` |
| Decode off the UI process | `glancelot-decoder`: content-sniffed allowlist, dims <= 32768, decoded RGBA8 <= 256 MiB, `QImageReader::setAllocationLimit(256)`, `setAutoTransform(true)`, source revision probed before **and** after, pixels via `QSharedMemory` | `src/worker/main.cpp` |
| Admission | `JobPriority{Foreground, Thumbnail, Prefetch, Metadata}`, `tryAdmit(jobId, priority, estimatedBytes, path)` | `include/glancelot/io_scheduler.hpp` |

**OPERATION-JOURNAL (B2) is not a prerequisite**, and that is worth stating plainly: export creates new files and never mutates a source, so there is nothing to undo on the originals. The one exception is an explicit `overwrite` of a *previous export's* output, which should be refused by default and, if ever allowed, treated like any other destructive write.

New components:

- `include/glancelot/image_export.hpp` (a new contract, lead-frozen) - `ImageExportRequest{generation, items, destinationFolder, naming, format, quality, resize, colorPolicy, metadataPolicy, collision, revalidate, maxItems}` and a result mirroring `ExportResult`. Deliberately the same shape as `selection_export.hpp` so the two read alike.
- `src/core/image_export.cpp` - orchestration only: no decoding, no encoding of its own.
- Encode in the worker process. The decoder worker already owns every image parser; extend it with an `--encode` request (source path or an already-decoded shared-memory buffer in, destination temp path out) so neither decode nor encode ever runs in the GUI process. The parent keeps the rename, the collision policy and the ledger.
- A preset model plus the export dialog (preview dimensions, estimated output size, the metadata policy statement).

Owner: **Track D**, as a sibling packet of **D8** ("F046 + F032 export + F045 AI metadata"). D8 already builds the export worker family; F071 should be the third consumer of it, not a fourth implementation.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance (latest release + date) | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| **Qt only** - `QImageWriter` JPEG, PNG, WebP, TIFF | Qt LGPL-3 (dynamic); Qt's bundled libjpeg-turbo/libpng/libwebp/libtiff | Yes | Qt 6.8.3, already pinned | nothing new; WebP/TIFF come from `qtimageformats`, already in the Windows dev tree | WebP/TIFF **blocked until EN-02** |
| libwebp used directly | BSD-3-Clause [1] | Yes | v1.6.0, 9 Jul 2025 (newest tag; the project publishes no GitHub releases) [2] | an extra library duplicating what Qt already bundles | same |
| libavif (+ an AV1 encoder: aom / SVT-AV1 / rav1e) | libavif BSD-2-Clause [3]; encoders have their own terms | needs per-encoder review | libavif v1.4.2, 26 May 2026 [4] | large; an AV1 encoder is the heaviest thing in this document | same |
| libjxl (`cjxl`) | BSD-3-Clause [5] | Yes | v0.12.0, 01 Jul [6] | large; still pre-1.0 | same |
| Write our own encoder | n/a | n/a | n/a | n/a | n/a |

"Write our own encoder" is a non-starter and is listed only to dismiss it: Qt already bundles mature encoders, and hand-writing a JPEG or PNG encoder would add risk for no benefit. The interesting question is not *which library* but *what Qt's writers actually do*, because that decides the metadata policy.

### What Qt 6.8's writers actually do (checked in the sources)

| Format | Quality / options | ICC profile | EXIF | Notes |
| --- | --- | --- | --- | --- |
| JPEG | `quality` 0-100 (default 75), `optimize` (`optimize_coding`), `progressive` | **Written** as APP2 chunks from `image.colorSpace().iccProfile()`, split across markers [7] | **Not written.** The handler only *reads* EXIF, to recover orientation (`readExifHeader`, `getExifOrientation`) [7] | so an exported JPEG carries colour but no camera metadata |
| PNG | `CompressionRatio` -> `png_set_compression_level` | **Written** as `iCCP`, named from `QColorSpace::description()` [8] | n/a for PNG; `png_set_text` writes `QImage` text keys [8] | |
| WebP | `quality` < 100 -> lossy at that quality; `quality` == 100 -> **lossless** with effort 70 | **Written** via the WebP mux ICC chunk [9] | not written | one knob covers lossy and lossless |
| TIFF | compression option; writes `Grayscale16`, `RGBX64`/`RGBA64`, and the 16/32-bit float formats | via `QImage` colour space | not written | useful for a high-precision output preset |

So the brief's assumption needs one correction: **`QImageWriter` drops EXIF but does preserve ICC**, for JPEG, PNG and WebP alike, provided the `QImage` handed to it carries a colour space. That turns the metadata policy from a problem into a decision:

- **Recommended v1 policy:** bake orientation (the decoder already applies `setAutoTransform(true)`, so pixels arrive upright and an orientation tag would double-rotate), preserve the colour profile by tagging the output, and **state in the UI that all other metadata is removed** - EXIF, GPS, XMP and maker notes. That is honest, it is the privacy-safe default F046 wants, and it needs no EXIF writer.
- Preserving selected EXIF fields means writing our own APP1 block (we already have a bounded EXIF *reader* in `src/app/metadata_parser.cpp` with a 256 KiB read cap, so the structure is understood) - that is F046's "clear preserved/removed fields" work, and it should land in D8, not here.

### Resampling quality

`QImage::scaled(..., Qt::SmoothTransformation)` uses Qt's `qimagescale`, an Imlib2-derived smooth scaler (the file is licensed BSD-2-Clause AND Imlib2, with SSE4.1 paths) [10]. It is a reasonable area/bilinear-class filter and it supports the 16-bit and float formats, but it is **not** Lanczos3, so it is not what VIEW-05 promises ("Lanczos3, bicubic, Mitchell and area downsampling"). Two honest options:

1. **v1:** use `Qt::SmoothTransformation`, label the preset's resampler by name in the UI, and do not claim Lanczos. Also pre-scale in halves for very large reductions (repeated 2:1 box steps down to within 2x of the target, then one smooth step) to avoid aliasing on big minifications.
2. **Later:** when VIEW-05's resampler lands in Track D, reuse it for export so the exported pixels match what the canvas showed. That is the right long-term answer and another reason not to write a private Lanczos here.

## 3. Platform capability

- **Windows**: fully supported with no new dependency. `qtimageformats` is already in the Windows dev tree, so WebP and TIFF presets work. No change to `scripts/build.ps1 -Package` and no new notices.
- **Native Linux**: same, where `qtimageformats` is present. JPEG and PNG are in qtbase and always available.
- **Docker/noVNC**: works, with two real limits the UI must state rather than discover at write time.
  1. `/photos` is bind-mounted **read-only** in `compose.yaml`. An export therefore cannot be written next to the source. The only writable location is the named `/config` volume. The destination picker must default there in the container and must refuse `/photos` with a clear reason, not an I/O error.
  2. There is **no download path**. `docker/start.py` runs Xtigervnc, openbox, the viewer and websockify; browser upload/drop is not implemented and neither is a file download (`docs/DOCKER.md`). So a container user can produce files inside `/config` but cannot fetch them through noVNC. That makes F071 in the container useful only to someone who can reach the volume by other means, and the docs must say so.
  3. `qtimageformats` is absent until EN-02, so only JPEG and PNG presets exist there; the capability model (D2) must report that rather than silently omitting formats from the preset list.
- All clients share one desktop and one cursor in the container, so a long export must not block the single session - the existing queue-and-progress pattern covers this.

## 4. Security model

- **Untrusted input stays behind the existing boundary.** Decode happens in `glancelot-decoder`; encode should too. Nothing in the GUI process parses or writes image bytes.
- **Never overwrite originals, by construction, not by policy.** Reuse `selection_export`'s two hard rules: the destination can never resolve to a selected/source item (`DestinationIsItem` must be refused *even when* `overwrite` is set), and the write goes to a temporary sibling that is renamed into place, so a crash or cancel leaves neither a partial output nor a damaged original. Default collision policy `Skip` or `RenameBoth`, never `Overwrite`.
- **Revalidate source identity** before each encode (`FileIdentity` + revision), because a batch export is long-running; the worker already probes the source revision before and after decoding and fails with "Source revision changed while decoding" - keep that and surface it as a per-item skip rather than a batch failure.
- **Bound everything**: items per request (`maxItems`), decoded bytes per item through the ledger, output pixel dimensions (a resize preset must not be allowed to request 100000 x 100000), and the number of concurrent encodes. A 32768 x 32768 source at 4 bytes per pixel is already 4 GiB of RGBA, far above the 256 MiB decode ceiling, so the preset UI must show the estimate and the engine must refuse rather than try.
- **Disk-full and permission errors** must produce `Failed` with the real reason and leave no temporary file; test with a deliberately tiny filesystem or a write-protected destination.
- **Output naming is attacker-adjacent too**: names derived from source filenames must go through `MutationService::validateTargetName`, must not inherit control characters or trailing dots/spaces (Windows), and must not be allowed to collide case-insensitively on Windows while looking distinct.
- **Metadata removal is a privacy feature**: if the UI says metadata is removed, a test must verify the output bytes contain no APP1/EXIF and no GPS - F046's "verify exported bytes" criterion applies here too.

## 5. Test strategy

Generated media only, nothing committed:

- Sources painted with `QPainter` and written in-test: PNG with alpha, JPEG, 16-bit TIFF, an image with a non-sRGB colour space attached, an image with an EXIF orientation tag (write the APP1 block by hand, as the project's own EXIF tests already imply is possible), a 1x1 image, and an extremely wide 1 x 20000 image.
- **Round-trip and policy assertions**: JPEG output at quality 50 and 95 (assert size ordering and that both decode); PNG output preserves alpha exactly; WebP at quality 100 is lossless (decode and compare pixels exactly); output carries an ICC profile when the source had one; output contains **no** EXIF/GPS bytes; orientation is baked (pixels match the upright source) and no orientation tag is present.
- **Resize**: target dimensions honoured exactly, aspect ratio preserved when asked, a large minification produces no aliasing worse than a reference (compare against an independently computed box average within a tolerance), and the resampler name in the result matches what was requested.
- **Collision and safety**: destination exists -> `DestinationExists` with default policy and nothing written; destination resolves to the source item -> refused even with `overwrite` true, and the source's bytes are hash-identical afterwards; `RenameBoth` produces a deterministic, validated name.
- **Failure and cancellation**: `stop_token` mid-batch (completed items remain, the in-flight item leaves no temporary file, the result is `Cancelled`); worker killed mid-encode; destination folder read-only; destination full; source deleted or modified between snapshot and encode (`Missing`/`Changed` skip); a result whose generation is stale is dropped.
- **Container case**: an export targeting `/photos` is refused with a named reason rather than failing on `EROFS`.
- New test file `tests/image_export_ui_tests.cpp` plus a core target in a lead-provided `cmake/ImageExportTests.cmake`; never added to `tests/grid_ui_tests.cpp`. Note that export tests write real files, so they must use a per-test temporary directory and clean up, and `ThumbnailService::shutdown()`'s precedent applies - stop workers before destroying temporary media.

**Stays unverified / RUNBOOK:** perceptual quality judgements ("does quality 85 look right"), throughput on real hardware and network shares, and whether a given third-party tool accepts our outputs. Also unverified until EN-02: WebP and TIFF presets in the container.

## 6. Effort estimate

| Packet | Content | Size |
| --- | --- | --- |
| **D9a** | `image_export.hpp` contract (lead-frozen) + `src/core/image_export.cpp` orchestration + worker `--encode` mode; JPEG and PNG only; metadata policy "ICC preserved, everything else removed"; orientation baked; temp-sibling + rename; collision, revalidation, cancellation | M |
| **D9b** | Resize and preset model: long-edge / exact / percentage, `Qt::SmoothTransformation` with halving pre-pass, preview dimensions and estimated output size, named resampler; WebP and TIFF presets behind the capability model | M |
| **D9c** | UI: preset management, destination policy (including the container's read-only `/photos` refusal), progress/pause/cancel wired to the existing queue, explicit metadata statement | M |

L in total, Track D, three packets, after D8 so the export worker family exists once. Prerequisites: D1 DECODE-TIER (so an export decode is a tier request rather than a bespoke decode), D8 (export worker + F046's metadata policy work), E1 RESOURCE-LEDGER (encode buffers in the same budget), D2 CAPABILITY (so the preset list reflects which writers exist on this platform), EN-02 (WebP/TIFF in the container). No DEP-DECISIONS entry is needed for the recommended slice - that is the main argument for it.

## 7. Recommendation and what would change it

**GO**, for a Qt-only slice: JPEG and PNG first, WebP and TIFF behind the capability model, resize via Qt's smooth scaler with its name shown, ICC preserved, all other metadata removed and *said* to be removed, and every write going through the `selection_export` safety pattern. This adds no dependency, no notice, no packaging change and no journal requirement, and it reuses three pieces of machinery the project already has.

**DEFER AVIF and JPEG XL encoding.** libavif is BSD-2-Clause [3] and libjxl BSD-3-Clause [5], so licence is not the obstacle; the obstacle is that AVIF encoding means adopting an AV1 encoder (aom, SVT-AV1 or rav1e), which would be the largest dependency in the project by a wide margin, and libjxl is still pre-1.0 at v0.12.0 [6]. Both also belong to the FMT-04 decode decision in `docs/DEP-DECISIONS.md`; export should follow that decision rather than force it.

**DEFER Lanczos3.** Promote it when VIEW-05's resampler lands in Track D, then reuse that code so exported pixels match displayed pixels. Writing a second resampler here would guarantee they disagree.

**Do not use libwebp directly.** Qt already bundles it and `QImageWriter`'s WebP path exposes both lossy quality and lossless [9]; a direct dependency would duplicate a library we already ship.

What would change this:

1. If the lead will not accept "EXIF removed" as the v1 default, F071 must wait for F046's metadata-preservation work, because a correct EXIF writer is a real piece of work and belongs there. That makes F071 **DEFER until D8 is integrated** rather than GO alongside it.
2. If a requirement row is added demanding byte-exact Lanczos3 output parity with the canvas, sequence F071 after VIEW-05's M5 alternatives.
3. If the container becomes a primary export target, F071 needs a download path out of noVNC first - that is F074/F076-family work, not this packet, and without it the container story stays "writes to `/config`, retrieve it yourself".

## Sources

All accessed 2026-10-22.

1. libwebp `COPYING` (BSD-3-Clause, Google Inc.) - https://raw.githubusercontent.com/webmproject/libwebp/main/COPYING
2. libwebp tags - newest v1.6.0, 9 Jul 2025; the repository's releases page states "There aren't any releases here" - https://github.com/webmproject/libwebp/tags
3. libavif licence file (BSD-2-Clause) - https://raw.githubusercontent.com/AOMediaCodec/libavif/main/LICENSE
4. libavif releases, newest v1.4.2 dated 26 May 2026 - https://github.com/AOMediaCodec/libavif/releases
5. libjxl licence file (BSD-3-Clause) - https://raw.githubusercontent.com/libjxl/libjxl/main/LICENSE
6. libjxl releases, newest v0.12.0 dated 01 Jul (the page prints no year; asset timestamps indicate 2026) - https://github.com/libjxl/libjxl/releases
7. Qt 6.8 JPEG handler - `quality` (default 75), `optimize_coding`, `progressive`; writes the ICC profile as APP2 markers from `image.colorSpace().iccProfile()`; reads EXIF only to recover orientation and writes none - https://raw.githubusercontent.com/qt/qtbase/6.8/src/plugins/imageformats/jpeg/qjpeghandler.cpp
8. Qt 6.8 PNG handler - writes `iCCP` from the image colour space, writes text chunks, supports `CompressionRatio` - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/image/qpnghandler.cpp
9. Qt 6.8 WebP handler - quality < 100 is lossy at that quality, quality == 100 switches to lossless with effort 70; writes the ICC profile - https://raw.githubusercontent.com/qt/qtimageformats/6.8/src/plugins/imageformats/webp/qwebphandler.cpp
10. Qt 6.8 `qimagescale.cpp` - Imlib2-derived smooth scaler, "SPDX-License-Identifier: BSD-2-Clause AND Imlib2", with SSE4.1 paths; this is what `Qt::SmoothTransformation` uses, and it is not Lanczos - https://raw.githubusercontent.com/qt/qtbase/6.8/src/gui/painting/qimagescale.cpp
