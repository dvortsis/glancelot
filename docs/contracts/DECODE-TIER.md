# DECODE-TIER contract proposal (draft for lead freeze)

Status: **proposal**, not frozen. Base: `origin/main` `3893c77` for shared headers. Decode-cache diffs are against EN-05 tip `d355edd` (`origin/claude/en-05-viewer-sources`), which adds `src/app/decode_cache.{hpp,cpp}`. **This contract cannot be frozen until EN-05 is integrated.** Owner after freeze: Track D. Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) §6.

## 1. Purpose and consumers

Let the viewer show a fast, honestly labelled lower-fidelity image first, then refine it to full detail. Orientation, colour and geometry stay identical across tiers, and a late lower tier can never replace a higher one.

| Consumer | Uses |
| --- | --- |
| D1 (PERF-DECODE, F033) | Implements the worker protocol, ladder policy, cache key and fencing; fixes the >64 MP scaled fallback (Fit stays when Full is refused). |
| D2 (F043) | Reads the per-image tier and source dimensions from the descriptor for the HUD (CAPABILITY covers system facts only). |
| D5 (F034 tiles) | Uses the reserved `Tile` tier and `DecodeRegion`; defines tile semantics. |
| D6 (F038) | Labels "source versus displayed": a histogram on a Fit buffer must say so. |
| A5 (PERF-THUMB) | Shares size buckets with Fit where useful; thumbnails stay on their own service. |
| E1, E2 | Ledger adoption for retained tiers; per-tier cache statistics in diagnostics. |
| C3 (compare panes) | Each pane requests its own tier; fencing is per pane generation. |

## 2. Current state the contract extends

- `DecodeRequest` and `PixelBufferDescriptor` in `contracts.hpp` are declared but **used nowhere**. This contract puts them to use as the in-process description of a request and result. They are not a wire format.
- The real contracts are:
  - **Worker wire protocol** (`src/worker/main.cpp`): argv `[path]`, then JSON lines `probe`, the admission key, `decoded`, `ok`. The `--source-revision` and `--thumbnail-server` modes are separate.
  - **`DecodeCache`**: key is the `QString` from `cacheKey(file)` plus a source token.
  - **`DisplayManager::updateImage(path, image, generation)`**: rejects only `generation < current`, so a later call with an *equal* generation is accepted whatever its fidelity.
  - **Status strings**, such as "Cached · display profile not yet managed".
- RAW today decodes the embedded JPEG preview and presents it as the image. Under this contract that result is labelled **Preview, final** (PERF-03).

## 3. Tiers

```
Preview  <  Fit  <  Full          (one whole-image fidelity ladder; rank 1, 2, 3)
Tile                               (region of the Full raster; separate channel, never replaces the base image)
```

- **Preview:** an embedded preview (RAW JPEG, EXIF thumbnail) or any cheap whole-image decode. Its size is whatever the source holds.
- **Fit:** a codec-scaled whole-image decode at a size bucket of at least the viewport's physical long edge. Buckets are powers of two from 512 to 8192 px long edge, so `sizeBucket` is the bucket edge.
- **Full:** the full source resolution (today's raster path).
- **Tile:** a region of Full at full resolution, owned by D5.

**Invariants at every tier of one source revision:**
- (a) pixels are display-oriented, with the EXIF transform applied in the worker exactly as today (`setAutoTransform(true)`; scale in stored orientation, then transform);
- (b) pixels are converted to the same working space by the same code path (today sRGB `RGBA8888`), so `colorSpace` is identical;
- (c) `sourceWidth` and `sourceHeight` (display-oriented full-resolution size) are identical;
- (d) viewport geometry (zoom percentage, pixel coordinates, normalised centre) is defined in **source** pixels, never buffer pixels.

## 4. Proposed header changes

### 4.1 `include/glancelot/contracts.hpp` (diff)

```diff
-struct DecodeRequest { AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U}; };
+// DECODE-TIER. Rank orders the whole-image ladder; Tile is outside it (rank 0) and never replaces a base image.
+enum class DecodeTier : std::uint8_t { Preview, Fit, Full, Tile };
+constexpr int tierRank(DecodeTier t) { return t == DecodeTier::Preview ? 1 : t == DecodeTier::Fit ? 2 : t == DecodeTier::Full ? 3 : 0; }
+const char* decodeTierName(DecodeTier t);              // "preview" | "fit" | "full" | "tile" (wire and QML spelling)
+std::optional<DecodeTier> parseDecodeTier(std::string_view name);
+// Region in Full-resolution, display-oriented source pixels. Tile only; empty otherwise.
+struct DecodeRegion { int x{}, y{}, width{}, height{}; bool empty() const { return width <= 0 || height <= 0; } };
+struct DecodeRequest {
+    AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U};
+    DecodeTier tier{DecodeTier::Full};                 // default = today's behaviour
+    int sizeBucket{};                                  // Fit: bucket long edge in physical px; 0 otherwise
+    DecodeRegion region;                               // Tile only
+    std::uint64_t generation{};                        // selection (or pane) generation that owns the request
+};
 enum class PixelFormat { Rgba8, Rgba16Float };
-struct PixelBufferDescriptor { int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{}; };
+struct PixelBufferDescriptor {
+    int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{};
+    DecodeTier tier{DecodeTier::Full};
+    int sourceWidth{}, sourceHeight{};                 // display-oriented full source size; 0 = unknown (some Preview)
+    bool finalTier{true};                              // no higher tier will follow (e.g. RAW without sensor decode)
+    std::string sourceToken;                           // verified revision token; empty = unverified, not cacheable
+};
```

(`<optional>` and `<string_view>` are added to the includes.)

### 4.2 `include/glancelot/display_manager.hpp` (diff): close the equal-generation gap

```diff
+#include "contracts.hpp"
@@
-    void updateImage(const QString& path, const QImage& image, quint64 generation);
+    // Same generation: rejected only when tierRank(tier) < tierRank(current tier) (a late lower tier).
+    // An equal tier is accepted: idempotent re-publishes stay valid (currentImageRequested handler at
+    // viewer_controller.cpp:401-402, timed publishing at display_manager.cpp:125/571, the clear at :1019).
+    // A newer generation always resets the tier floor. The defaults keep today's callers unchanged.
+    void updateImage(const QString& path, const QImage& image, quint64 generation,
+                     DecodeTier tier = DecodeTier::Full, QSize sourceSize = {});
```

`IPresentationWindow::setPrimaryImage` stays. D1 adds a source-size setter on the presentation canvas so Fit renders at source geometry. That is a Track D render file, not a shared header.

### 4.3 `include/glancelot/viewer_controller.hpp` (diff)

```diff
+    // "preview" | "fit" | "full"; "" when no image. imageWidth/imageHeight keep meaning source size (equal to the
+    // buffer for Full, so unchanged today); when the source size is unknown (sourceWidth == 0, e.g. a RAW Preview)
+    // they and all viewport geometry fall back to the buffer size. imageFinal is false while a refinement is pending.
+    Q_PROPERTY(QString imageTier READ imageTier NOTIFY imageChanged)
+    Q_PROPERTY(bool imageFinal READ imageFinal NOTIFY imageChanged)
```

### 4.4 `src/app/decode_cache.hpp` (diff against EN-05 `d355edd`; Track D owned, not a shared header)

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
-    bool contains(const QString& key) const;
-    QImage reuse(const QString& key,const QString& sourceToken) const;
-    bool insert(const QString& key,QImage pixels,const QString& sourceToken);
-    bool remove(const QString& key);
+    bool contains(const DecodeCacheKey& key) const;
+    // Dominance: returns the highest whole-image tier cached for key.source whose rank >= the requested rank
+    // (and, for Fit, whose bucket >= the requested bucket), with a matching non-empty token. `hit` reports the tier.
+    QImage reuse(const DecodeCacheKey& key, const QString& sourceToken, DecodeTier* hit = nullptr) const;
+    // Rejected when a higher whole-image tier with the same token is already cached (no downgrade).
+    // Storing a whole-image tier removes lower tiers of the same source and token. Capacity unchanged (128 MiB).
+    bool insert(const DecodeCacheKey& key, QImage pixels, const QString& sourceToken);
+    int removeSource(const QString& source);       // every tier of one source (revision mismatch, delete)
```

`Tile` entries are not stored by this cache. D5 defines a tile cache under the ledger.

### 4.5 Worker wire protocol (`src/worker/main.cpp`; Track D)

- **Arguments.** argv `[path]` means Full, unchanged. New forms: `[--tier, fit, --edge, N, path]` and `[--tier, preview, path]`. Any other combination is a protocol error.
- **Messages.** `probe` and `decoded` add `"tier"`, `"sourceWidth"`, `"sourceHeight"` and `"final"`. The parent validates as follows:
  - the echoed tier must equal the requested tier;
  - for Fit, `max(width, height) <= edge`;
  - the source dimensions must be at most 32768 and identical across tiers of one token;
  - the existing byte, stride and 256 MiB checks still apply.
  
  A mismatch fails the request as an invalid decoder response.
- **Allowlist.** The duplicated format allowlist (`main.cpp:103,232`) becomes one function shared by all tiers. CAPABILITY reads that same list.

## 5. Ownership, threading, fencing and bounds

- **GUI thread:** `ViewerController` owns the ladder, `DecodeCache` and the shown tier. Decoding happens only in the worker process. No tier is decoded or scaled on the GUI or render thread. Scaling for Fit happens in the worker.
- **Sequential ladder:** at most **one** in-flight decode per selection generation. Full starts after Fit is applied or fails, never concurrently (PERFORMANCE-STRATEGY: no doubled memory). Full admission keeps counting the shown Fit image (`image.sizeInBytes()` is already in the allowance) until E1 replaces it.
- **Apply rule (the core fence):** the controller applies decode result R only when all of the following hold:
  - `R.generation == generation`;
  - tokens agree when **both** are non-empty. An empty token, meaning the revision probe was not `known` on a weak or network filesystem, may still refine the display; it is only not cached;
  - `tierRank(R.tier) > tierRank(shownTier)`, or nothing is shown for this generation.
  
  Equal or lower decode results are dropped, with the perf flag `Stale`. A source revision change already starts a new generation (`viewer_controller.cpp:351`), so it is the only way to reset the floor. `DisplayManager::updateImage` adds defence in depth: it rejects only a strictly lower tier at equal generation and accepts equal-tier re-publishes.
- **Failure:**
  - if Full fails or is refused after Fit is shown, Fit stays and the status says so ("Scaled preview: <reason>"), with `imageFinal = true` and tier `fit`;
  - if Fit fails, the ladder falls through to Full;
  - nothing unrelated replaces a valid image (F061).
- **Cancellation:** navigation kills the in-flight worker as today, and no queued Full survives a generation change.
- **Ladder policy (D1, tunable):** use Fit then Full only when the source has at least 2x the pixels of the Fit bucket. Otherwise go straight to Full. RAW is Preview final until a sensor decoder exists.
- **Bounds:** cache 128 MiB (unchanged); at most 3 whole-image tiers per source, and in practice 1, because a higher insert evicts lower; per-tier probe limits as today.

## 6. Every cache reader and writer (migration without behaviour change)

| Site | Tier after migration | Behaviour |
| --- | --- | --- |
| Foreground decode (`decode()`, `phase=="decoded"`) | Full, bucket 0 | Key `encoded()` equals today's string; identical. |
| Revision-only reuse (`revisionOnly` path) | Request Full; dominance returns Full only (no other tier is ever stored before D1) | Identical. |
| `cache.contains` before revision-only | `contains({key, Full})` | Identical. |
| Prefetch (`startPrefetch`, `viewer_controller.cpp:606`) | Full (D1 may switch prefetch to Fit later) | Identical. |
| `isLastDecodeCacheHit` | True when any tier at or above the requested one was reused. Documented on the property. | Identical while only Full exists. |
| `decodedCacheBytes/Count`, `clearDecodedCache` | All tiers | Identical. |
| RAW path | Labelled `preview`, `final=true` | Pixels and status text unchanged; `imageTier` reports `preview`. |
| `DisplayManager::updateImage` callers | Default `Full` | Identical. |

Before D1 enables the ladder, the only observable change is `imageTier` (`full`, or `preview` for RAW).

## 7. Test plan (generated media only)

Extend `tests/cache_tests.cpp` through the lead's cmake; add `tests/decode_tier_tests.cpp` (worker plus controller, offscreen).

- **Identity across tiers:** a generated 4000x3000 JPEG with EXIF orientation 6 and an embedded Display P3 profile, and the same as untagged PNG. Fit (bucket 1024) is compared with Full downscaled to the same size in the test:
  - same orientation, so the aspect ratio is swapped correctly;
  - mean channel difference at most 2/255, with no hue shift on saturated patches;
  - same `colorSpace` and source dimensions.
- **Fencing:**
  - Full applied, then a late Fit for the same generation, is dropped (controller and `DisplayManager`);
  - Fit for generation N arriving after navigation to N+1 is dropped;
  - an equal-tier decode result arriving twice is dropped by the controller, while `DisplayManager` accepts an equal-tier re-publish (opening a presentation after Full still shows the image);
  - Full with an empty token after Fit still refines the display and is not cached;
  - a revision change (new generation) shows a new Fit even though Full of the old revision was shown.
- **Cancellation:**
  - navigate away during Fit, and again during Full: no worker survives, nothing applied, admission released once;
  - kill the worker mid-Full: Fit stays and the status is honest.
- **Failure:**
  - Full exceeds the 256 MiB or headroom limit: Fit stays, labelled scaled (the >64 MP fix);
  - a truncated JPEG makes Fit fail, then Full also fails with the corrupt-versus-unsupported message;
  - a worker echoes the wrong tier: invalid response;
  - Fit result larger than the bucket: rejected;
  - source dimensions differ between tiers: rejected.
- **Cache:**
  - Full key byte-identical to the EN-05 key (regression);
  - dominance: Full cached, Fit request reuses it;
  - no downgrade on insert;
  - higher insert evicts lower;
  - token mismatch misses every tier;
  - capacity accounting covers all tiers.
- **Sequential memory:** a hook asserts that no two decode processes for one generation overlap.

## 8. Open questions for the lead

1. **Fit buckets.** Accept power-of-two buckets (512 to 8192) and the 2x-pixels ladder threshold, or should Fit share A5's thumbnail buckets?
2. **Keep the Preview tier after Full?** Eviction-on-higher (proposed) saves memory, but a fast reverse-navigation preview might want to keep a small Preview.
3. **Wire versus struct.** Should D1 serialise `DecodeRequest` and `PixelBufferDescriptor` directly into the JSON protocol (one schema), or keep the hand-written fields above?
4. **Decoder version in the key.** The cache is memory-only and process-lifetime, so the key omits a decoder or colour-pipeline version. Add one now for a future disk cache, or defer?
5. **`DisplayManager` overload versus fencing only in the controller.** The overload is defence in depth and touches a shared header. Approve both?
6. **Persistent worker.** D1's experiment behind a switch must keep this apply rule. Should the lead freeze a per-request reset message now, or leave it to D1's plan?

## 9. Deliberately not covered

- Tile semantics, the tile cache, region decode capability and pan cancellation (D5). Only the enum value and `DecodeRegion` are reserved.
- Higher-precision formats (`Rgba16Float`), HDR, and output or monitor colour (D4, COLOR-04). Tiers do not change the working space.
- Resource ledger accounting (E1); thumbnails (`ThumbnailService` is unchanged); animation frames (D1 handles them separately).
- System capability reporting (CAPABILITY). Per-image tier facts live here, in `PixelBufferDescriptor`.
- RAW sensor decode and new codecs (EN-09 dependency decisions).
