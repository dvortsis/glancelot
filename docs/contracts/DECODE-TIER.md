# DECODE-TIER contract

Status: **frozen** by PHASE2-CONTRACTS. Code references are at `b1d57cd`. D1 starts after PR 3 (`claude/stack-confirm-fix`) merges, because PR 3 changes the transfer path. Owner: Track D. Implementing packet: D1 (PERF-DECODE, F033). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md). Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) section 6.

## Purpose

Let the viewer show a fast, honestly labelled lower-fidelity image first, then refine it to full detail. Orientation, colour and geometry stay identical across tiers, and a late lower tier can never replace a higher one.

## Consumers

| Consumer | Uses |
| --- | --- |
| D1 (PERF-DECODE, F033) | Implements the worker protocol, ladder policy, cache key and fencing; fixes the >64 MP scaled fallback (Fit stays when Full is refused). |
| D2 (F043) | Reads the per-image tier, source dimensions and `imageColour` for the HUD (CAPABILITY covers system facts only). D2 depends on D1. |
| D5 (F034 tiles) | Uses the reserved `Tile` tier and `DecodeRegion`; defines tile semantics. |
| D6 (F038) | Labels "source versus displayed": a histogram on a Fit buffer must say so. |
| A5 (PERF-THUMB) | Keeps its own thumbnail buckets; does not share Fit's. |
| E1, E2 | Ledger adoption for retained tiers; per-tier cache statistics in diagnostics. |
| C3 (compare panes) | Each pane requests its own tier; fencing is per pane generation. |

## Current state the contract extends

- `DecodeRequest` and `PixelBufferDescriptor` in `contracts.hpp` were declared and unused. This contract puts them to use as the in-process description of a request and result. They are not a wire format.
- The real contracts are:
  - **Worker wire protocol** (`src/worker/main.cpp`): argv `[path]`, then JSON lines with `phase` `probe`, the admission key, `decoded`, `ok`. The `--source-revision` and `--thumbnail-server` modes are separate.
  - **`glancelot::detail::DecodeCache`** (`src/app/decode_cache.hpp`, EN-05): the key is the `QString` from `Private::cacheKey(file)` (`viewer_controller.cpp:545`) plus a source token.
  - **`DisplayManager::updateImage(path, image, generation)`** (`display_manager.cpp:597`): rejects only `generation < current` (`:602`), so a later call with an *equal* generation is accepted whatever its fidelity.
  - **Status strings**, such as "Cached · display profile not yet managed" (`viewer_controller.cpp:1001`, `:1047`).
- RAW today decodes the embedded JPEG preview and presents it as the image. Under this contract that result is labelled **Preview, final** (PERF-03).

## Tiers

```
Preview  <  Fit  <  Full          (one whole-image fidelity ladder; rank 1, 2, 3)
Tile                               (region of the Full raster; separate channel, never replaces the base image)
```

- **Preview:** an embedded preview (RAW JPEG, EXIF thumbnail) or any cheap whole-image decode. Its size is whatever the source holds.
- **Fit:** a codec-scaled whole-image decode at a size bucket of at least the viewport's physical long edge. Buckets are powers of two from `kFitBucketMinEdge` (512) to `kFitBucketMaxEdge` (8192) px long edge, so `sizeBucket` is the bucket edge.
- **Full:** the full source resolution (today's raster path).
- **Tile:** a region of Full at full resolution, owned by D5.

**Invariants at every tier of one source revision:**

- (a) pixels are display-oriented, with the EXIF transform applied in the worker exactly as today (`setAutoTransform(true)`; scale in stored orientation, then transform);
- (b) pixels are converted to the same working space by the same code path (today sRGB `RGBA8888`), so `colorSpace` is identical;
- (c) `sourceWidth` and `sourceHeight` (display-oriented full-resolution size) are identical;
- (d) viewport geometry (zoom percentage, pixel coordinates, normalised centre) is defined in **source** pixels, never buffer pixels.

## Frozen interface

### In the tree now: `include/glancelot/contracts.hpp`

`DecodeTier`, `tierRank`, `decodeTierName`, `parseDecodeTier`, `kFitBucketMinEdge`, `kFitBucketMaxEdge`, `DecodeRegion`, and the appended defaulted fields on `DecodeRequest` (including `refresh`) and `PixelBufferDescriptor`. The defaults mean Full, today's behaviour. Nothing outside the compile test uses them until D1.

### Applied by D1 (D1's brief grants these exact diffs)

`include/glancelot/display_manager.hpp`, which closes the equal-generation gap:

```diff
+#include "contracts.hpp"
@@
-    void updateImage(const QString& path, const QImage& image, quint64 generation);
+    // Same generation: rejected only when tierRank(tier) < tierRank(current tier) (a late lower tier). A refresh is never
+    // lower than what's shown, so it needs nothing more here.
+    // An equal tier is accepted: idempotent re-publishes stay valid (the currentImageRequested handler at
+    // viewer_controller.cpp:479-481, timed publishing at display_manager.cpp:138 and :654, the clears at
+    // viewer_controller.cpp:232 and :1097). A newer generation always resets the tier floor. The defaults keep
+    // today's callers unchanged.
+    void updateImage(const QString& path, const QImage& image, quint64 generation,
+                     DecodeTier tier = DecodeTier::Full, QSize sourceSize = {});
```

`IPresentationWindow::setPrimaryImage` stays. D1 adds a source-size setter on the presentation canvas so Fit renders at source geometry. That is a Track D render file, not a shared header.

`include/glancelot/viewer_controller.hpp`:

```diff
+    // "preview" | "fit" | "full"; "" when no image. imageWidth/imageHeight keep meaning source size (equal to the
+    // buffer for Full, so unchanged today); when the source size is unknown (sourceWidth == 0, e.g. a RAW Preview)
+    // they and all viewport geometry fall back to the buffer size. imageFinal is false while a refinement is pending.
+    Q_PROPERTY(QString imageTier READ imageTier NOTIFY imageChanged)
+    Q_PROPERTY(bool imageFinal READ imageFinal NOTIFY imageChanged)
+    // {"tagged":bool, "description":string} for the applied image; empty when none. Set with imageTier under the apply rule.
+    Q_PROPERTY(QVariantMap imageColour READ imageColour NOTIFY imageChanged)
```

`src/app/decode_cache.hpp` (Track D owned, not a shared header):

```diff
+struct DecodeCacheKey {
+    QString source;                               // cacheKey(file), unchanged
+    DecodeTier tier{DecodeTier::Full};
+    int sizeBucket{};                             // Fit only
+    // Full -> source verbatim (byte-identical to today, so `cache` tests are unaffected);
+    // other tiers -> source + '\x1f' + tierName + ':' + bucket.
+    QString encoded() const;
+};
 class DecodeCache {
 public:
-    bool contains(const QString& key) const { return cache.contains(key); }
-    QImage reuse(const QString& key,const QString& sourceToken) const;
-    bool insert(const QString& key,QImage pixels,const QString& sourceToken);
-    bool remove(const QString& key) { return cache.remove(key); }
+    bool contains(const DecodeCacheKey& key) const;
+    // Dominance: returns the highest whole-image tier cached for key.source whose rank >= the requested rank
+    // (and, for Fit, whose bucket >= the requested bucket), with a matching non-empty token. `hit` reports the tier.
+    QImage reuse(const DecodeCacheKey& key, const QString& sourceToken, DecodeTier* hit = nullptr) const;
+    // Rejected when a higher whole-image tier with the same token is already cached (no downgrade).
+    // Storing a whole-image tier removes lower tiers and other Fit buckets of the same source and token, and every entry of
+    // that source under another token. Capacity unchanged (128 MiB).
+    bool insert(const DecodeCacheKey& key, QImage pixels, const QString& sourceToken);
+    int removeSource(const QString& source);       // every tier of one source (revision mismatch, delete)
```

`Tile` entries are not stored by this cache. D5 defines a tile cache under the ledger. The key carries no decoder or colour-pipeline version, because this cache is memory-only and lives for one process. Any future disk cache must add one.

### Worker wire protocol (`src/worker/main.cpp`; Track D)

- **Arguments.** argv `[path]` means Full, unchanged. New forms: `[--tier, fit, --edge, N, path]` and `[--tier, preview, path]`. Any other combination is a protocol error.
- **Messages.** Hand-written JSON fields, as today; `DecodeRequest` and `PixelBufferDescriptor` are not serialised. `probe` and `decoded` add `"tier"`, `"sourceWidth"`, `"sourceHeight"` and `"final"`. The parent validates as follows:
  - the echoed tier must equal the requested tier;
  - for Fit, `max(width, height) <= edge`;
  - the source dimensions must be at most 32768 and, when both tokens are non-empty and equal, identical across tiers (a different or empty token is a new revision or an unproven one, so new dimensions are not an error);
  - the existing byte, stride and 256 MiB checks still apply.

  A mismatch fails the request as an invalid decoder response.
- **Allowlist.** The duplicated format allowlist (`src/worker/main.cpp:103` and `:232`) becomes one function shared by all tiers. CAPABILITY reads that same list.
- **Persistent worker (D1's experiment behind a switch).** Any persistent protocol must carry a per-request id echoed on every message, reset all per-request state between requests, keep the hard deadline and crash isolation (PERFORMANCE-STRATEGY section 7), and obey the same apply rule. A message whose id is not the current request's is dropped. The rest of its shape is D1's.
- **Transfer buffers.** The class is whatever PR 3 lands. The GUI thread never takes a cross-process lock on a transfer buffer and never blocks on one, and each tier has its own buffer. RESOURCE-LEDGER states when its bytes are released.
- **Fit's allocation bound.** A named `kFitMaxSourceBytes` per format class, in the worker. JPEG scales in the codec, so it may exceed 256 MiB of source, with a hard cap set by D1's measurement. PNG, TIFF, WebP and the others decode whole before scaling, so they keep today's 256 MiB source cap and go straight to the refusal text.

## Ownership, threading and bounds

- **GUI thread:** `ViewerController` owns the ladder, `DecodeCache` and the shown tier. Decoding happens only in the worker process. No tier is decoded or scaled on the GUI or render thread. Scaling for Fit happens in the worker.
- **Sequential ladder:** at most **one** in-flight decode per selection generation. Full starts after Fit is applied or fails, never concurrently (PERFORMANCE-STRATEGY section 7: no doubled memory). Full admission keeps counting the shown Fit image (`image.sizeInBytes()` is already in the allowance, `viewer_controller.cpp:1020`) until E1 replaces it.
- **Apply rule (the core fence):** the controller applies decode result R only when `R.generation == generation` and one of these holds:
  - nothing is shown for this generation;
  - `tierRank(R.tier) > tierRank(shownTier)`. The decodes of one generation run one at a time, so a result is never older than what's shown, and a different token here simply means a fresher revision (Fit under T1, then Full under T2, applies and ends with `imageFinal` true). An empty token, meaning the revision probe was not `known` on a weak or network filesystem, may still refine the display; it is only not cached;
  - R came from a **refresh** request and its rank equals the shown rank.

  Other results are dropped, with the perf flag `Stale`. `DisplayManager::updateImage` adds defence in depth: it rejects only a strictly lower tier at equal generation and accepts equal-tier re-publishes.
- **Refresh (EN-11).** Most source revisions take a new generation through `ViewerController::open` (`viewer_controller.cpp:1702`). EN-11's late baseline deliberately does not: `Private`'s observation handler (`:428`) runs `cancel();cacheReuseAllowed=false;quietDecode=background;decode(path,generation);` in the same generation, because it must not disturb the open's own listing or a newer request. The pixels on screen may then be stale, so that decode is a refresh (`DecodeRequest::refresh`), and its result must replace them.
  - A refresh never starts below what's shown, so the view never drops in quality: with Full shown it requests Full; with Fit shown it requests Fit at the shown bucket, then the ladder continues; with a final Preview it requests Preview. So its results reach `DisplayManager` at an equal or higher tier.
  - It cancels any refinement in flight, under the one-in-flight rule. `imageFinal` is false only while a higher tier is pending.
  - A file being rewritten can't loop: each observation starts at most one refresh, as EN-11 does today, and a ladder ends at its last tier.
- **Priority:** the first tier of a generation runs at `Foreground`, and refinement at `Visible`, so background work competes with a refinement and never with the first paint. In the ledger, every tier of the current image reserves as `Foreground` whatever its job priority, so a Full that today's limits admit can still use the headroom (RESOURCE-LEDGER). Under `Quality` reclaim (RESOURCE-LEDGER) a pending Full doesn't start, which leaves Fit shown with `imageFinal = true` and the honest status; shown pixels are never swapped for lower ones.
- **Viewer state during a ladder:** the first applied tier sets `imageReady = true` and `loading = false`, as a decode does today, with `imageFinal = false` until the last tier. Phase 1 code keeps working unchanged: `currentImageRequested` still publishes, and passes the shown tier and source size (`viewer_controller.cpp:481`), so re-published Fit pixels stay labelled; `DisplayManager` keeps them with its pending frame for the timed paths (`display_manager.cpp:138`, `:654`), `navigating` (PR 3) settles when the destination is handed to `open()` and does not wait for a refinement, and the slideshow's clock runs from the first applied tier. `scheduleLookahead` waits for `imageFinal`, so prefetch never competes with a refinement. The status text keeps naming the tier.
- **Frames held elsewhere:** `pinReference`, `pinCurrentImage` and `setComparisonImage` pin whatever is shown. When Full arrives for the same generation and path, the controller re-offers it to the holder through the same call, so the reference or pinned output refines too. If the budget refuses, the holder keeps its lower tier, labelled. The F040 and pin refusal rules in `DisplayManager` are unchanged.
- **Ladder policy (D1):** use Fit then Full only when the source has at least 2x the pixels of the Fit bucket. Otherwise go straight to Full. RAW is Preview final until a sensor decoder exists. Both are named constants; changing them needs D1's measurements in its handoff.
- **Bounds:** cache 128 MiB (unchanged); at most 3 whole-image tiers per source, and in practice 1, because a higher insert evicts lower; per-tier probe limits as today.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Full fails or is refused after Fit is shown | Fit stays and the status says so ("Scaled preview: <reason>"), with `imageFinal = true` and tier `fit` |
| Fit fails | The ladder falls through to Full |
| Both fail | The usual failure path; nothing unrelated replaces a valid image (F061) |
| Worker killed mid-Full | Fit stays; the status is honest |
| Worker echoes the wrong tier, Fit larger than its bucket, or source dimensions differ between tiers of one non-empty token | Fails as an invalid decoder response |
| Navigation | Kills the in-flight worker as today; no queued Full survives a generation change |
| Worker killed mid-transfer | The buffer is released and the GUI thread does not wait |
| Fit's source exceeds `kFitMaxSourceBytes` | No Fit is attempted; the refusal text, as for Full |

## Every cache reader and writer (migration without behaviour change)

| Site (`viewer_controller.cpp`) | Tier after migration | Behaviour |
| --- | --- | --- |
| Foreground decode, `phase=="decoded"` insert (`:1043`) | Full, bucket 0 | Key `encoded()` equals today's string; identical |
| Revision-only reuse (`:991`) and its remove (`:995`) | Request Full; dominance returns Full only (no other tier is ever stored before D1); remove becomes `removeSource` | Identical |
| `contains` before revision-only (`:946`) | `contains({key, Full})` | Identical |
| Baseline proof after registration (`:425`) | `reuse` at the shown tier and bucket, with the `registered` token, so a late baseline with current Fit pixels starts no refresh | Identical while only Full exists |
| Late baseline refresh (`:428`) | `decode` with `refresh` set; with `cacheReuseAllowed` false it skips the cache and inserts as usual | Identical while only Full exists: the result replaces the shown image, and `controllerLateCoalescedBaselineDecodesOnce` still passes |
| Prefetch insert (`:755`), `contains` before prefetch (`:832`) | Full (D1 may switch prefetch to Fit later) | Identical |
| Allowance (`:727`, `:1020`), `clear` (`:471`, `:891`, `:899`, `:1021`, `:1930`, `:2106`, `:2175`) | All tiers | Identical |
| `isLastDecodeCacheHit` | True when any tier at or above the requested one was reused. Documented on the property | Identical while only Full exists |
| `decodedCacheBytes/Count`, `clearDecodedCache` | All tiers | Identical |
| RAW path | Labelled `preview`, `final=true` | Pixels and status text unchanged; `imageTier` reports `preview` |
| `DisplayManager::updateImage` callers | Default `Full` until the ladder is on. D1 passes the shown tier and size at `:481` and in the controller's own publishes | Identical before the ladder |
| Status text, `decoded.color` (`:1047`) | D1 parses `tagged` and the description into `imageColour`. The status string is unchanged until D2 | Identical |

Before D1 enables the ladder, the only observable change is `imageTier` (`full`, or `preview` for RAW).

## Acceptance tests D1 must pass

Generated media only. Extend `tests/cache_tests.cpp` through the lead's cmake; add `tests/decode_tier_tests.cpp` (worker plus controller, offscreen).

- **Identity across tiers:** a generated 4000x3000 JPEG with EXIF orientation 6 and an embedded Display P3 profile, and the same as untagged PNG. Fit (bucket 1024) is compared with Full downscaled to the same size in the test:
  - same orientation, so the aspect ratio is swapped correctly;
  - mean channel difference at most 2/255, with no hue shift on saturated patches;
  - same `colorSpace` and source dimensions.
- **Fencing:**
  - Full applied, then a late Fit for the same generation, is dropped (controller and `DisplayManager`);
  - Fit for generation N arriving after navigation to N+1 is dropped;
  - a non-refresh equal-tier decode result arriving twice is dropped by the controller, while `DisplayManager` accepts an equal-tier re-publish (opening a presentation after Full still shows the image);
  - Full with an empty token after Fit still refines the display and is not cached;
  - a revision change (new generation) shows a new Fit even though Full of the old revision was shown.
- **Refresh (EN-11):**
  - `tests/fs_watch_tests.cpp` `controllerLateCoalescedBaselineDecodesOnce` passes unchanged, including `fresh-pixels`, with the ladder off and on;
  - a late-baseline refresh replaces an equal tier, with Full shown, and the view never shows a lower tier;
  - the same refresh with Fit shown goes to Fit, then Full, and ends with `imageFinal` true;
  - Fit under T1 and Full under T2 applies Full and ends with `imageFinal` true, with the old token's cache entries gone;
  - a file with new dimensions between tiers is not an invalid response;
  - a file rewritten repeatedly starts at most one refresh per observation;
  - a late baseline with current Fit pixels starts no refresh and leaves the refinement running.
- **Colour:** `imageColour` is set with the applied tier, and a result dropped by the fence changes nothing; a tagged and an untagged image report differently.
- **Phase 1 state:**
  - with the ladder on, `imageReady` turns true at the first tier, `imageFinal` at the last, and `navigating` falls without waiting for refinement;
  - a slideshow step during refinement cancels it and the next photo shows;
  - a reference pinned at Fit refines to Full when it arrives, and keeps Fit, labelled, when the budget refuses.
- **Cancellation:**
  - navigate away during Fit, and again during Full: no worker survives, nothing applied, admission released once;
  - kill the worker mid-Full: Fit stays and the status is honest.
- **Failure:**
  - Full exceeds the 256 MiB or headroom limit: Fit stays, labelled scaled (the >64 MP fix);
  - a generated PNG above the source cap gets no Fit and the refusal text, and a re-published Fit keeps its tier and source size on the presentation;
  - a truncated JPEG makes Fit fail, then Full also fails with the corrupt-versus-unsupported message;
  - a worker echoes the wrong tier: invalid response;
  - Fit result larger than the bucket: rejected;
  - source dimensions differ between tiers of one non-empty token: rejected.
- **Cache:**
  - Full key byte-identical to the current key (regression);
  - dominance: Full cached, Fit request reuses it;
  - no downgrade on insert;
  - higher insert evicts lower, and a Fit insert evicts other Fit buckets of that source;
  - token mismatch misses every tier;
  - capacity accounting covers all tiers.
- **Sequential memory:** a hook asserts that no two decode processes for one generation overlap.

## Not covered

- Tile semantics, the tile cache, region decode capability and pan cancellation (D5). Only the enum value and `DecodeRegion` are reserved.
- Higher-precision formats (`Rgba16Float`), HDR, and output or monitor colour (D4, COLOR-04). Tiers do not change the working space.
- Resource ledger accounting (E1); thumbnails (`ThumbnailService` is unchanged); animation frames (D1 handles them separately).
- System capability reporting (CAPABILITY). Per-image tier facts live here, in `PixelBufferDescriptor`.
- RAW sensor decode and new codecs (EN-09 dependency decisions).
