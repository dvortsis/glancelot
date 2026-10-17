# Glancelot requirements and acceptance matrix

Baseline: October 17, 2026. Rows describe target acceptance, not blanket implementation claims; consult the work queue and verification records for actual delivered evidence. The phase is the intended first complete delivery; prototypes may appear earlier. M0-M6, including the approved M2.1 priority milestone, form the proposed v1; E1-E3 are expansion milestones defined in [the build plan](BUILD-PLAN.md). “Conditional” means a dependency/platform/format feasibility gate must pass before the feature can be promised. Approved amendment: core folder tree moves from M3 to the next milestone M2.1; UX-12 adds persistent numbered-window browsing locations. There are now 88 requirement rows.

Each implementation issue should reference these IDs and record the exact tests, platform coverage, screenshots/traces where useful, and remaining limitations. Preserve the [original input](original-feature-specification.txt) when requirements change; record amendments explicitly.

## 1. Inputs and traversal

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| IN-01 | Unified action registry; keyboard, modifiers, mouse buttons, click variants, wheel and gestures | M2 basic; M3 complete | Every user-facing command is discoverable/remappable; equivalent input paths produce the same state transitions; unavailable OS inputs are explained. |
| IN-02 | Fullscreen, windowed canvas, grid and comparison contexts | M3; compare M4 | Same key resolves correctly in each scope; text fields and modals take precedence; no context leaks. |
| IN-03 | Wheel navigation/zoom/pan matrix and modifier overrides | M3 | Mouse detents and high-resolution trackpad deltas behave predictably; cursor-centered zoom and horizontal pan verified. |
| IN-04 | Fuzzy command palette with active bindings and remapping access | M2 | Keyboard-only discovery/execution/remap; disabled actions explain prerequisites; current binding remains in sync. |
| IN-05 | Two-step chords and spring-loaded holds | M3 | Timeout/cancel/conflict cases tested; hold restores state on release, focus loss, pointer cancel and image change. |
| IN-06 | Portable human-readable preferences, layouts and bindings | M1 schema; M3 UI | One versioned JSON survives Windows/Linux round trip; OS-specific entries, migrations, corrupt config, and safe reset tested. |
| NAV-01 | Strict depth-first folder traversal | M2 | Property tests and fixture trees prove preorder directory sequence and reverse equivalence; deterministic natural comparator documented. |
| NAV-02 | Forward-first/backward-last folder landing | M2 | Explicit folder jumps land at correct eligible endpoints, independent of current file sort. |
| NAV-03 | Stop, current-folder loop and auto-advance boundaries | M2 | All three modes tested forward/backward, one-file/empty folders and end-of-root; root wrapping is separate. |
| NAV-04 | Empty, excluded, unsupported and permission-denied folder bypass | M2 | No stall/infinite loop; cancellation works; diagnostics expose skipped locations without interrupting ordinary traversal. |
| NAV-05 | Clear temporary filters on successful directory crossing | M2 | Candidate search excludes temporary filter, commit clears it; failure preserves original state; persistent filters and tags survive. |
| NAV-06 | Folder order independent of file sorting | M2 name; M3 all sorts | Name, modified/capture date, size, dimensions and seeded shuffle leave folder order unchanged; ties/missing metadata stable. |
| NAV-07 | Cross-folder asynchronous lookahead | M3 | Trigger by final three eligible images; first target preview ready when workload permits; reversal/filter changes discard stale work. |
| NAV-08 | Traversal root, links, volume boundaries and filesystem changes | M2-M3 | No unexpected drive-wide scan; symlink/junction cycles bounded; rename/delete/atomic-save and remote disconnect handled. |

## 2. Rendering and performance

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| PERF-01 | Hardware-accelerated canvas; high-refresh pan/zoom/rotation | M0 proof; M2-M3 | D3D12/Vulkan qualified per platform with explicit fallbacks; frame/presentation distributions meet named hardware budgets. |
| PERF-02 | Fast native startup; <100 ms ambition | M0 baseline; M6 gate | First correctly displayed image timed from shell launch; cold/warm caches and preview/full detail reported separately; stretch goal not universal guarantee. |
| PERF-03 | RAW embedded-preview-first, optional sensor decode | M3 preview; E2 full | Extraction/read/decode timing separated; absent/low-resolution previews identified; full-decode swap preserves viewport and labels quality. |
| PERF-04 | Bidirectional decoded cache with RAM/VRAM limits | M2; tune M3 | Next/previous hits, pressure eviction and admission tested; includes worker/staging allocations and 4-up cases; no unrestricted full-res texture ring. |
| PERF-05 | Storage-aware I/O concurrency | M3 | SSD/HDD/SMB fixtures and manual override; adaptive metrics and cancellation; no UI blocking or decoder thread oversubscription. |
| PERF-06 | Huge images, GPU loss, resource limits | M3-M6 | Large dimensions and allocation overflow rejected safely; tiled/scaled decode where supported; device-loss and suspend recovery preserve state. |

## 3. Interface and windows

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| UX-01 | Frameless zero-chrome launch, optional image-sized window | M2; sizing M3 | Window can always be moved, resized and closed; decorated mode available; work-area bounds and fractional DPI tested. |
| UX-02 | Top HUD: filmstrip, filters, sort | M3 | Virtualized large-folder strip; active item visible; controls operate by keyboard; preview generation does not stall canvas. |
| UX-03 | Collapsible left HUD: live directory tree and favorites | M2.1 NEXT: core tree; M3: favorites | Expand/collapse, lazy cancellable child loading, active-folder indication and selection, keyboard navigation, remappable show/hide and pin/auto-hide; no eager recursive scan. Validate dark/light themes and narrow widths using ui-design-skills. Favorites follow in M3 without delaying the tree. |
| UX-04 | Right HUD: EXIF, histogram and ICC status | M3 metadata; M5 histogram | Clear preview/source/color status; expensive calculations async; no metadata wait on initial display. |
| UX-05 | Bottom HUD: zoom, size, pixel coordinates, index, actions | M3 | Physical zoom accurate; orientation-aware coordinates; unknown count shows scanning; menu/exit routes remain available. |
| UX-06 | Four edge triggers with configurable delay and pinning | M3 | Dwell, corner arbitration, focus persistence, active-drag suppression and reduced-motion behavior pass usability tests. |
| UX-07 | Black/dark gray/middle gray/white/custom/ambient/checker backgrounds | M3 presets; M5 ambient | Single command cycles chosen presets; checker alpha correct; ambient bounded and optional; hex values persisted. |
| UX-08 | Single-instance reuse versus independent windows | M3 | Shell-open races, IPC startup, failed recipient recovery, per-window state and shared-config writes tested. |
| UX-09 | Always-on-top reference, 20-100% opacity and click-through | M5 conditional per platform | Actual capability detected; reliable recovery outside click-through canvas; unsupported controls disabled with reasons. |
| UX-10 | Controller plus clean second-monitor presentation | M5 | Controller UI never appears on output; output-specific color/scale; monitor unplug and focus changes recover. |
| UX-11 | Modern minimal visual system and accessibility | M0 designs; M6 gate | Cohesive controls/icons/text, screen-reader names, keyboard routes, text scaling/contrast, five-workflow usability record. |
| UX-12 | Persist last folder/image independently for each window-opening-order slot | M2.1 | First/second/etc windows retain stable slots; close order does not change ownership. After complete app exit, reopen windows one by one and restore each slot's own folder/image. Retain unopened slots; save atomically during browsing and on close/exit; coordinate independent processes. Explicit file opens take precedence. Test sort changes, missing image/folder, inaccessible/empty sources and write interruption without cross-slot or theme/binding data loss. |

## 4. Zoom, pan and resampling

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| VIEW-01 | Shrink-only, fit, fill, fit-width and fit-height modes | M2 | Orientation-corrected formula tests for landscape/portrait/small images and changing viewports. |
| VIEW-02 | Zoom and normalized pan lock across image changes | M4 | Different dimensions/aspect ratios and rotations use documented anchor/clamp behavior; no accumulated drift. |
| VIEW-03 | Hold loupe at 100/200/400% and configurable values | M2 basic; M3 settings | Cursor anchor stable; exact prior view restored on all release/cancel paths; click/double-click conflicts resolved. |
| VIEW-04 | 100% means one image pixel per physical display pixel | M0 proof; M2 | Checkerboard/pixel-grid verification at 100/125/150/175/200% OS scale and mixed-DPI transitions. |
| VIEW-05 | Lanczos3, bicubic, Mitchell and area downsampling | M3 default; M5 alternatives | CPU-reference comparisons, large minification anti-aliasing, transparency/fringe tests and performance measurements. |
| VIEW-06 | Optional post-resize sharpening | M5 | Disabled by default in exact inspection; no metadata/source mutation; excessive halos prevented by bounded controls. |
| VIEW-07 | Nearest/bicubic/Lanczos upscaling hotkey | M3 nearest/bicubic; M5 Lanczos | Instant switch without re-decode; pixel-art nearest output exact; persistent selection matches settings. |

## 5. Advanced viewing and diagnostics

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| ADV-01 | 2-up/4-up synchronized comparison | M4 | Pan/zoom link modes, active candidate swaps and removal; memory budgets include every pane; removing pane never deletes file. |
| ADV-02 | Archives behave as virtual folders without disk extraction | E1 | ZIP/CBZ first then qualified families; deterministic traversal, entry cancellation, bounded decompression, no archive payload temp files. |
| ADV-03 | RGB/alpha channel isolation, invert and luminance grayscale | M5 | Reference pixel outputs; color-domain labels; alpha/opaque-image behavior and view-only persistence tested. |
| ADV-04 | Highlight/shadow warnings with optional blinking | M5 | Bit-depth-aware thresholds, any/all-channel policy, HDR-domain distinction, reduced-motion/non-blinking alternative. |
| ADV-05 | Full-image and visible-region RGBA/luminance histograms | M5 whole; E2 region | Exact oracle counts for known fixtures; approximate mode labeled; crop/orientation/alpha and async freshness correct. |
| ADV-06 | GIF/WebP/APNG playback and TIFF page navigation | M5 | Correct frame durations/disposal/blending/loop policy, bounded cache; TIFF pages not assigned invented timing. |
| ADV-07 | Animation timeline, 0.1-4x speed, loop, forward/back step and extraction | E2 | Seeking/checkpoint replay correct; speed uses monotonic time; export color/orientation and overwrite rules tested. |
| ADV-08 | Animated AVIF/JXL variants | E2 conditional by decoder | Sequence fixtures prove actual decode/timing support; still-only support never advertised as animation support. |
| ADV-09 | Apple Live Photos and Android motion photos | E2 | Paired-asset and embedded-payload fixtures, missing partner behavior, user-triggered playback, audited video codec build. |
| ADV-10 | Continuous vertical strip | E1 | Virtualized image/layout working set; padding control, 10,000-item scrolling, tall-image and cancellation tests. |
| ADV-11 | Dual-page spreads, LTR/RTL and cover offset | E1 | Odd/even counts, cover toggles, direction changes and folder/archive boundaries preserve correct ordering. |
| ADV-12 | On-canvas hex/RGB/float/CMYK/HSL/Lab sampling | M5 RGB/hex; E2 full | Source versus displayed sample explicit; profile/white point supplied; alpha and HDR out-of-range values not silently clipped. |
| ADV-13 | Thirds/golden-ratio/crosshair/custom pixel guides | M5 | Viewport versus image anchoring defined; correct transforms at rotation/zoom; no alteration to copied original/source. |

## 6. Color and HDR

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| COLOR-01 | Current-monitor ICC awareness and transition handling | M0 proof; M4 | SDR reference patches on tested displays; spanning-window ownership, profile updates and dual-output behavior; fallback labeled. |
| COLOR-02 | Embedded sRGB/AdobeRGB/P3/ProPhoto and untagged fallback | M2 baseline; M4 full | CPU color oracle and invalid-profile fixtures; no mandatory 8-bit intermediate or double compositor transform. |
| COLOR-03 | Proof in sRGB command | M5 | Explicit gamut/proof semantics and rendering intent; reversible view transform; does not falsely imply print soft-proofing. |
| COLOR-04 | Integer/float input precision and native HDR output | M0 capability proof; E3 | Source precision preserved, actual negotiated surface format documented; calibrated output measurements; SDR fallback and UI white correct. |
| COLOR-05 | ISO 21496-1 gain maps and Ultra HDR JPEG | E3 | Independently labeled variants and conformance fixtures; gain/headroom sweeps and SDR base-layer fallback. |
| COLOR-06 | Apple Adaptive HDR HEIC | E3 conditional | Actual exported-file variants decoded and compared on qualified displays; not inferred from ordinary HEIC support. |
| COLOR-07 | EXR/Radiance SDR exposure/highlight compression preview | E3 | Float/negative/nonfinite sample policy, reversible tone map, channel selection limitations and numeric tests. |

## 7. Metadata, culling and handoff

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| META-01 | Categorized EXIF/IPTC/XMP inspection | M3 basic; M4 full | Lazy bounded parsing, orientation/profile distinction, raw tag access, malformed/large metadata handling. |
| META-02 | Stable Diffusion/ComfyUI/other AI metadata and prompt/workflow copy | E2 | Versioned PNG tEXt/iTXt and WebP fixtures; size/nesting limits; schema fallback; payload never executed. |
| META-03 | GPS map-provider handoff | M4 | Valid coordinates and explicit browser invocation; malformed/missing GPS handled; no automatic external request. |
| META-04 | Non-exclusive file access and hot reload | M3 | External overwrite/rename/delete during viewing on Windows/Linux; validated replacement swap; local reaction target measured. |
| META-05 | 1-5 stars, pick/reject and color labels via `.xmp` sidecars | M4 | Per-property Lightroom/Capture One/darktable fixture matrix, sidecar naming collisions, preserved unknown XML, save conflicts and errors. |
| META-06 | FastStone-style temporary tagging and tagged-only filter | M4 | Space/Insert remappable, cross-directory tags stable, temporary filter reset follows NAV-05, selection distinct from ratings. |
| META-07 | Tagged batch copy/move/delete/path copy | M4 | Stable target snapshot, paired-file policy, collisions, cross-volume verify, trash fallback confirmation, cancellation/partial-failure recovery. |
| META-08 | Lossless JPEG rotation/flips | M5 | Separate from view rotation; EXIF and perfect MCU cases, edge dimensions, orientation normalization, metadata preservation and recovery tested. |
| META-09 | Bitmap/file-object/path/Base64 clipboard choices | M4 bitmap/file/path; M5 Base64 | Native Windows and Linux transfer formats; encoded/rendered color semantics documented; resource limits/cancellation tested. |
| META-10 | Nine external-tool slots, single/tagged arguments | M4 | Argument arrays, Unicode/spaces/metacharacters, missing tools, working directory and command-length limits; no shell interpolation. |

## 8. Complete requested extension inventory

Each row is a target inventory, not a support claim. The package's generated capability page must show shipped decoder version and proven subfeatures. Native libraries are bundled or locally linked; no paid OS codec dependency is assumed.

| ID | Extensions / assets | Target phase | Qualification required |
| --- | --- | --- | --- |
| FMT-01 | `.jpg`, `.jpeg`, `.png` | M2 | Grayscale/RGB/CMYK where relevant, EXIF orientation, ICC, transparency, bit depth and malformed fixtures. |
| FMT-02 | `.bmp`, `.tga`, `.tif`, `.tiff`, `.gif` | M3-M5 | Raster variants, TIFF multiple pages/high precision, GIF animation/disposal; publish unsupported compression/variants. |
| FMT-03 | `.webp`, `.apng` | M3-M5 | Still and animated capability tracked separately, including alpha and timing. |
| FMT-04 | `.avif`, `.jxl`, `.qoi` | M5 stills; E2 sequences | Bit depth/alpha/ICC/orientation validated; AVIF/JXL animation conditional on adapter support. |
| FMT-05 | `.heic`, `.heif` | M5 conditional SDR; E3 HDR | Codec/backend/license qualification, auxiliary images and transforms; gain maps not assumed. |
| FMT-06 | `.dng`, `.cr2`, `.cr3`, `.nef`, `.nrw`, `.arw`, `.sr2`, `.raf`, `.orf`, `.rw2`, `.pef`, `.iiq` | M3 previews; E2 full | Camera model/variant fixture matrix, preview dimensions and profile, missing-preview fallback. Extension alone insufficient. |
| FMT-07 | `.exr`, `.hdr`, Ultra HDR `.jpg`, gain-map `.heic`, 16/32-bit `.tiff` | TIFF M5; others E3 | Numeric precision and SDR rendering separate from HDR output; EXR deep/multipart/channel limitations explicit. |
| FMT-08 | `.svg`, `.svgz` | E3 | Bounded rasterization, text/font policy, no scripts/remote resource fetching, compressed-input limits. |
| FMT-09 | `.psd`, `.psb`, `.kra`, `.clip` | E3; CLIP conditional | Flattened/merged-preview extraction; absent composite is reported; no full editor/layer rendering promise. |
| FMT-10 | `.dds`, `.ktx2` | E3 | Compression/transcode, mip levels, array/cube/volume slices qualified individually. |
| FMT-11 | `.ico`, `.icns` | ICO M5; ICNS E3 | Multiple resolutions and embedded codec variants, selection of displayed size. |
| FMT-12 | `.zip`, `.cbz`, `.rar`, `.cbr`, `.7z`, `.tar` | E1 | Encryption/solid archives/seek behavior and limits; no extraction to disk; archive revision invalidates cached entries. |
| FMT-13 | Apple Live Photos, Android motion photos; “Motion JPEG” wording in input | E2 | Distinguish paired media, embedded MP4/video and actual MJPEG streams; publish exactly which assets/codecs work. |

## 9. Cross-cutting gates

| ID | Requirement | Phase | Acceptance evidence |
| --- | --- | --- | --- |
| SYS-01 | Offline, no telemetry/cloud/catalog | M1-M6 | Clean install opens files without network; no background outbound requests; delete cache and rebuild without losing user metadata. |
| SYS-02 | Robust resource and parser boundaries | M1-M6 | Bounded IPC/allocations, worker crash/deadline recovery, fuzz corpus, sandbox enforcement capability documented. |
| SYS-03 | Portable configuration without destructive migration | M1-M6 | Atomic save, schema migration/future schema fallback, last-good backup, multi-instance conflicts and safe-mode recovery. |
| SYS-04 | Windows/Linux packaging and contributor build | M1 skeleton; M6 release | Fresh-machine install/open/upgrade/uninstall, desktop associations, local codecs, documented reproducible build inputs and notices. |
| SYS-05 | Regression and performance evidence | M0-M6 | Exact benchmark hardware/workloads, p50/p95/p99 results, CI coverage, 30-minute memory soak, physical display validation. |
| SYS-06 | Source-file and metadata safety | M4-M6 | No implicit writes while viewing; every mutation has explicit targets, conflict handling and honest recovery limits. |

## Change-control rule

A feature may move between milestones only with an updated row, a reason, and its dependencies. A conditional feature may remain unsupported; the documentation must say so. A requirement becomes verified only when evidence exists on the declared platform/format combinations. UX-03 was moved earlier and UX-12 added by explicit user approval on October 17, 2026; both remain planned pending implementation and evidence.
