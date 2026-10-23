# META-ROLES contract proposal (draft for lead freeze)

Status: **proposal**, not frozen. Base: `origin/main` `3893c77`. Owner after freeze: Track A. Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) §6.

## 1. Purpose and consumers

Give the grid, filmstrip and traversal one source of capture time, oriented pixel dimensions, rating and metadata state. Add `captured` and `dimensions` sorts that next/previous, the filmstrip and the grid all use, with no metadata read in a QML delegate.

| Consumer | Uses |
| --- | --- |
| A2 (F042 core) | Implements both paths below. Owns `MetadataKeyCache` and the reader. |
| A3 (F013 group headers) | Date and rating roles; `metaState` gives the "unknown" group. |
| A4 (F042 chips, F014 badges) | Rating and dimensions roles, counts over `metaState == ready`. |
| A6 (F009 columns), A7 (F008 jump to date) | Roles and sort keys. |
| C2 (F025 rating UI) | Rating role (read). C1 writes sidecars and invalidates rows. |
| C5 (F028 bursts) | `captureTime` role and the captured order. |

## 2. Two paths, deliberately separate

1. **Display path (visible window).** `requestMetadata(firstRow, lastRow)` fills roles for the rows on screen. It is bounded by the window and fenced by generation. It never changes order.
2. **Order path (whole folder).** `captured` and `dimensions` need a key for every image in the folder, because next/previous crosses the whole listing. The worker that already lists the folder reads the keys: the grid listing worker and the navigation worker. It is cancellable, admitted at `JobPriority::Metadata` and bounded per file and per folder. The order is published once when the keys are in, as A1 requires. Until then the previous order stays and `sortPending` is true. If the folder budget runs out, the unread files count as missing and go last, and `sortPartial` is true.

The two paths share one bounded `MetadataKeyCache`. Rows read for the order arrive with their roles already `ready`, and rows read for display speed up a later sort.

## 3. Proposed header changes

### 3.1 `include/glancelot/navigation.hpp` (diff)

```diff
+#include <functional>
@@
-enum class SortKey { Name, Modified, Size, Shuffle };
+// Captured: EXIF capture time as a timezone-less wall-clock value (see MetaKeys). Dimensions: display-oriented
+// pixel area, then width. Missing values are last in both directions; ties break by natural name.
+enum class SortKey { Name, Modified, Size, Shuffle, Captured, Dimensions };   // append only; values are not persisted
@@
-struct FileMeta { std::int64_t modified{}; std::uintmax_t size{}; bool known{}; };
+// `known` keeps its meaning (the stat succeeded) so existing positional initializers stay valid.
+// Each extended key has its own flag so "missing last" applies per key.
+struct FileMeta {
+    std::int64_t modified{}; std::uintmax_t size{}; bool known{};
+    std::int64_t captured{};              // wall-clock milliseconds, see MetaKeys::captureTime
+    std::uint32_t pixelWidth{}, pixelHeight{};
+    bool capturedKnown{}, dimensionsKnown{};
+};
+// Fills captured/dimension keys for `files` (same length as `meta`) on the calling worker thread.
+// It honours `cancel`, reads at most MetaKeys::kMaxReadBytes per file under IoScheduler Metadata admission,
+// and leaves a *Known flag false for any file it could not or would not read (budget, I/O failure, unsupported format).
+// Returns false when the folder budget ended the pass early (the result is then marked partial).
+using SortKeyReader = std::function<bool(const std::vector<std::filesystem::path>& files,
+                                         std::vector<FileMeta>& meta, std::stop_token cancel)>;
@@
 std::vector<std::filesystem::path> sortedImageFiles(const std::filesystem::path& directory, const SortSpec& sort,
-    std::stop_token cancel = {}, std::vector<FileMeta>* meta = nullptr);
+    std::stop_token cancel = {}, std::vector<FileMeta>* meta = nullptr, const SortKeyReader& keys = {});
@@ class Navigator
+    // Installed once by the app (core stays Qt-free). Empty: Captured/Dimensions treat every key as missing,
+    // which gives natural-name order, deterministic and identical across traversal, filmstrip and grid.
+    void setSortKeyReader(SortKeyReader reader) { keys_ = std::move(reader); }
+    bool sortPartial() const { return sortPartial_; }
 private:
+    SortKeyReader keys_;
+    bool sortPartial_{};
```

`directory_listing.hpp` gets the same trailing `const SortKeyReader& keys = {}` parameter on `listDirectory`, and `DirectoryListing` gains `bool sortPartial{false}`.

### 3.2 New `src/app/metadata_keys.hpp` (Track A owned; promote to `include/glancelot/` only if C needs it directly)

```cpp
#pragma once
#include <QString>
#include <cstdint>
#include <optional>
namespace glancelot {
enum class MetaState : std::uint8_t { Unrequested, Pending, Ready, Unavailable, Failed };
// Lightweight per-file keys, not the HUD's MetadataRecord. Parsed from at most kMaxReadBytes of the file header
// with a direct JPEG/TIFF/PNG/WebP header reader. No QImageReader, no pixel decode, never on the GUI thread.
struct MetaKeys {
    static constexpr qint64 kMaxReadBytes = 64 * 1024;
    // EXIF DateTimeOriginal (+SubSecTimeOriginal) as wall-clock ms on a fixed epoch, ignoring OffsetTimeOriginal
    // for ordering (camera clocks rarely carry a reliable offset). No fallback to file modified time.
    std::optional<std::int64_t> captureTime;
    std::optional<std::int16_t> captureOffsetMinutes;   // display only, when OffsetTimeOriginal exists
    std::uint32_t pixelWidth{}, pixelHeight{};          // display-oriented (EXIF orientation applied); 0 = unknown
    // -1 = rejected, 0 = explicitly unrated, 1..5 stars; nullopt = no rating found. Embedded EXIF 0x4746 or the
    // embedded XMP packet's xmp:Rating. Sidecar precedence belongs to C1 (see open questions).
    std::optional<std::int8_t> rating;
    MetaState state{MetaState::Unrequested};
};
// Thread-safe (internal mutex), bounded LRU keyed by (path key, size, modified). A stale revision is a miss.
// Holds at most kMaxEntries (equal to the listing cap, 100000; about 6 MB). Memory only, never written to disk.
class MetadataKeyCache {
public:
    static constexpr std::size_t kMaxEntries = 100000;
    static MetadataKeyCache& instance();
    std::optional<MetaKeys> find(const QString& pathKey, std::uintmax_t size, std::int64_t modified) const;
    void store(const QString& pathKey, std::uintmax_t size, std::int64_t modified, const MetaKeys& keys);
    void invalidate(const QString& pathKey);   // fs-watch, sidecar write (C1)
    void clear();                              // E2 "clear disposable caches"
    std::size_t size() const;
};
// Worker-thread reader used by both paths and installed as the SortKeyReader.
MetaKeys readMetaKeys(const QString& path, std::stop_token cancel);
}
```

### 3.3 `include/glancelot/directory_entry_model.hpp` (diff)

```diff
     enum Role { NameRole = Qt::UserRole + 1, PathRole, KindRole, LinkRole, HiddenRole, SizeRole, ModifiedRole,
-                SelectedRole, CursorRole, CurrentRole };
+                SelectedRole, CursorRole, CurrentRole,
+                // META-ROLES (appended; existing values unchanged). Undefined QVariant while unknown.
+                CaptureTimeRole, PixelWidthRole, PixelHeightRole, RatingRole, MetaStateRole };
+    Q_PROPERTY(bool sortPending READ sortPending NOTIFY sortStateChanged)   // keys still being read; old order shown
+    Q_PROPERTY(bool sortPartial READ sortPartial NOTIFY sortStateChanged)   // budget ended; unread files are last
@@
-    // "name" | "modified" | "size" | "shuffle"; folders stay first ...
+    // "name" | "modified" | "size" | "shuffle" | "captured" | "dimensions"; folders stay first ...
     Q_INVOKABLE void setSort(const QString& key, bool descending, quint64 seed = 1);
+    // Display path: fill metadata roles for rows [firstRow, lastRow] of the current listing. The range is clamped
+    // to the listing and to kMaxMetadataWindow rows (centred on the middle of the request). Each call replaces the
+    // previous window: queued rows outside it are cancelled. Folder, image and non-image rows: only image rows are
+    // read; others report metaState "unavailable" without I/O.
+    static constexpr int kMaxMetadataWindow = 512;
+    Q_INVOKABLE void requestMetadata(int firstRow, int lastRow);
+    bool sortPending() const; bool sortPartial() const;
 signals:
+    void sortStateChanged();
```

Role names: `captureTime` (QDateTime, `Qt::LocalTime` built from the wall-clock fields), `pixelWidth`, `pixelHeight` (int), `rating` (int), `metaState` (`"unrequested" | "pending" | "ready" | "unavailable" | "failed"`).

## 4. Ownership, threading, fencing and bounds

- **GUI thread:** `DirectoryEntryModel` and its role storage. `data()` never reads a file. It returns what is stored for the row, or undefined plus `metaState`.
- **Workers:** the display path uses the model's own `QThreadPool`, with at most `IoScheduler::maxConcurrentBackgroundWorkers(directory)` threads, never more than 2. The order path runs inside the existing listing or navigation job. Each file read is admitted through `admitIoJob(JobPriority::Metadata, kMaxReadBytes, path)`. A refused admission leaves the row `pending`, and it is retried on `admissionAvailable`.
- **Fencing:**
  - each `requestMetadata` call increments `metaGeneration`;
  - `setDirectory`, `setActive(false)`, `setSort`, `setIncludeHidden` and a listing apply all increment it and request a stop;
  - a result is applied only when its `metaGeneration` and listing `generation` both match, and only to the row found by **path key** at apply time, never by the row index captured at request time;
  - a result whose (size, modified) differs from the row's is discarded, and the row returns to `unrequested`.
- **Bounds:**
  - display queue: at most `kMaxMetadataWindow` pending rows, with at most 64 rows per `dataChanged` emission, coalesced on a 16 ms timer so GUI work per tick stays bounded;
  - per-file read: `kMaxReadBytes`;
  - folder budget for the order path: 20,000 image files, or 4 s of read time on `Network` storage, both lead-tunable constants;
  - cache: `kMaxEntries`.
- **Cancellation:** cooperative through `std::stop_token` between files. A blocked OS read cannot be interrupted, the same limit as listing.
- **Truncated listings:** sorting covers only the listed subset; `truncated` already says so.

## 5. Sort semantics (`fileSortBefore`)

- `Captured`: `capturedKnown` first in both directions, then `captured`, then natural name.
- `Dimensions`: `dimensionsKnown` first in both directions, then `pixelWidth*pixelHeight` (64-bit), then `pixelWidth`, then natural name.
- An empty or failing reader makes every key missing, so the result equals natural order. Shuffle, Name, Modified and Size are unchanged byte for byte.
- `needMeta` in `sortedImageFiles` becomes `Modified || Size || Captured || Dimensions`, so the reader receives each file's stat data (size and modified), which it needs as the cache revision. The reader is then called once over the listed files before sorting. Name and Shuffle still skip stat.

## 6. Migration without behaviour change

| Site | Change |
| --- | --- |
| `navigation.cpp` `sortBefore`, `needMeta` | Two new branches; the old keys go through the same code. |
| `viewer_controller.cpp:98` `sortKeyName` | Add `"captured"` and `"dimensions"`. |
| `viewer_controller.cpp:508` string parse | Accept both. Unknown strings still fall back to Name, so older builds read new settings as Name. |
| `viewer_controller.cpp:1598` `sort` cycle | **Unchanged** (four keys). The new keys come from the sort menu only (see open questions). |
| `directory_entry_model.cpp` `sortSpec()` | Accept both. |
| `Navigator` construction | The app calls `setSortKeyReader(...)` once. Tests without it keep current behaviour. |
| `FileMeta{a,b,c}` initializers (`directory_listing.cpp:78,114`, `navigation.cpp:102`) | Compile unchanged (fields appended with defaults). |
| `MetadataParser::parse` and the HUD | Untouched. The HUD keeps its full record; grid keys never go through it. |
| Roles | Appended after `CurrentRole`; existing role numbers and names unchanged. |

With no QML calling `requestMetadata` and no new sort selected, behaviour and I/O are identical to today.

## 7. Test plan (generated media only)

New `tests/meta_roles_tests.cpp` (core, no display) and `tests/meta_roles_ui_tests.cpp` (model plus QML grid), through a lead-provided `cmake/MetaRolesTests.cmake`.

- **Reader:** generated JPEGs with DateTimeOriginal, SubSec and Offset; no EXIF; orientation 6 (swapped dimensions); PNG IHDR; TIFF; a truncated APP1; a cyclic IFD; a 100 KiB APP1 beyond the read cap; EXIF Rating 0x4746 values -1, 0, 3 and 5; an XMP packet rating. Each case asserts the keys or the missing flags, and that nothing reads past `kMaxReadBytes`.
- **Sort:**
  - ascending and descending, with missing values last in both directions;
  - equal keys break ties by natural name;
  - an empty reader equals Name order;
  - traversal (`Navigator::step`), `sortedImageFiles` and `listDirectory` produce the same image order for the same folder.
- **Order path:**
  - cancel mid-pass (no publication, old order kept);
  - folder budget exhausted (`sortPartial`, unread files last);
  - a file changed during the pass (stale key discarded);
  - a listing replaced mid-pass by a newer generation (stale result dropped).
- **Display path:**
  - window clamped to 512 rows;
  - scroll storm: 50 windows in a row leave at most 512 queued and only the last window applied;
  - a result for a row that moved, by an inserted file, lands on the right path;
  - a result after `setDirectory` is dropped;
  - `setActive(false)` cancels;
  - admission refused leaves the row `pending`, then fills after release;
  - a non-image row is `unavailable` with no I/O (counted through a reader hook);
  - no `data()` call performs I/O (hook counter stays 0 while delegates scroll).
- **Budget:** 10k generated rows, with `requestMetadata` GUI work per tick under 8 ms (label `budget`).

## 8. Open questions for the lead

1. **Order-path budget and UX.** Is reading keys for the whole folder acceptable for `captured` and `dimensions` (20k files and 4 s on network storage by default), with `sortPending` keeping the old order until the keys are in? The alternative is to restrict these sorts to the grid's loaded rows, which breaks NAV-06 parity with traversal.
2. **Rating source.** Embedded EXIF and XMP only until C1. When C1 lands, does a sidecar override the embedded value (the Lightroom convention)? Where does that merge live: in `readMetaKeys` (reading `.xmp` on demand) or as a C1-owned overlay?
3. **Capture time.** Sort on the wall-clock value ignoring `OffsetTimeOriginal` (proposed), or normalise to UTC when an offset exists? Mixed sources then order differently.
4. **Sort cycle.** Add `captured` and `dimensions` to the `sort` command cycle (a behaviour change for existing users), or menu only (proposed)?
5. **Cache placement.** Keep `MetadataKeyCache` in `src/app` (proposed) or make it a shared header now, so C5 can read capture times for files outside the current listing?
6. **In-process parsing.** The reader parses untrusted headers in-process on a worker thread, as the HUD already does. Is that acceptable until F4 fuzzes it, or should keys come from the decoder process?

## 9. Deliberately not covered

- Writing ratings, labels or picks (C1, C2), and sidecar merge rules.
- Grouping, header rows and collapse (A3); chips and their counts (A4).
- Incremental listing and batched arrival (A1). This contract assumes A1's "definitive order published once" and adds only the key pass.
- HUD metadata (`MetadataModel`, `MetadataParser::parse`); its bit depth and colour space stay as they are (see CAPABILITY).
- Persisting metadata to disk; folder order (always natural, NAV-02); filters on metadata values.
