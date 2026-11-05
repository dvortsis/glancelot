# META-ROLES contract

Status: **frozen** by PHASE2-CONTRACTS. Code references are at `b1d57cd`. Owner: Track A. Implementing packet: A2 (F042 core). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md). Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) section 6.

## Purpose

Give the grid, the filmstrip and traversal one source of capture time, oriented pixel dimensions, rating and metadata state. Add `captured` and `dimensions` sorts that next/previous, the filmstrip and the grid all use, with no metadata read in a QML delegate.

## Consumers

| Consumer | Uses |
| --- | --- |
| A2 (F042 core) | Implements both paths below. Owns `MetadataKeyCache` and the reader. |
| A3 (F013 group headers) | Date and rating roles; `metaState` gives the "unknown" group. Calls `requestAllMetadata` while grouping by those values. |
| A4 (F042 chips, F014 badges) | Rating and dimensions roles, counts over `metaState == "ready"`. Calls `requestAllMetadata` while a chip is on; `metadataPartial` says the counts are incomplete. |
| A6 (F009 columns), A7 (F008 jump to date) | Roles and sort keys. A7 calls `requestAllMetadata` for a date jump. |
| C1, C2 (F025) | C2 reads the rating role. C1 installs the sidecar rating overlay and invalidates rows after a write. |
| C5 (F028 bursts) | `captureTime` role and the captured order. Calls `requestAllMetadata` while bursts are on. |

## Three paths, deliberately separate

1. **Display path (visible window).** `requestMetadata(firstRow, lastRow)` fills roles for the rows on screen. It is bounded by the window, fenced by generation and never changes order.
2. **Order path (whole folder).** `captured` and `dimensions` need a key for every image, because next/previous crosses the whole listing. The worker that already lists the folder reads the keys: the grid listing worker and the navigation worker. It is cancellable, admitted at `JobPriority::Metadata` and bounded per file and per folder. The order is published once when the keys are in, as A1 requires. Until then the previous order stays and `sortPending` is true. If the folder budget runs out, the unread files count as missing and go last, and `sortPartial` is true.

3. **Key pass (whole folder, no reorder).** `requestAllMetadata()` runs the order path's worker, budget, cancellation and fencing, for features that need every row's value under any sort. Rows become `ready` through the display path's `dataChanged` batching, and the order never changes. A feature calls it only while it is on.

All three share one bounded `MetadataKeyCache`. Rows read for the order arrive with their roles already `ready`, and rows read for display speed up a later sort.

## Frozen interface

### In the tree now: `include/glancelot/navigation.hpp`

`SortKey::Captured` and `SortKey::Dimensions` (appended; values are not persisted), the extended `FileMeta` fields (`captured`, `pixelWidth`, `pixelHeight`, `capturedKnown`, `dimensionsKnown`; appended, defaulted) and the `SortKeyReader` alias.

Until A2 lands both keys are unreachable, and they give natural order: `sortKeyName` knows them (`viewer_controller.cpp:99`), `sortBefore` treats every value as missing (`navigation.cpp:221`), and no string parses to them (`viewer_controller.cpp:591-595`, `:1987-1991`, `directory_entry_model.cpp:64`).

### Applied by A2 (A2's brief grants these exact diffs)

`include/glancelot/navigation.hpp`:

```diff
+// Process-wide, installed once by the app before the first worker runs (core stays Qt-free). Every Navigator, sortedImageFiles and
+// listDirectory reads it, so no construction site can miss it. Empty: Captured/Dimensions treat every key as missing, which gives
+// natural-name order, deterministic and identical across traversal, filmstrip and grid. Thread-safe to read.
+void setDefaultSortKeyReader(SortKeyReader reader);
 std::vector<std::filesystem::path> sortedImageFiles(const std::filesystem::path& directory, const SortSpec& sort,
-    std::stop_token cancel = {}, std::vector<FileMeta>* meta = nullptr);
+    std::stop_token cancel = {}, std::vector<FileMeta>* meta = nullptr, bool* partial = nullptr);
@@ class Navigator
+    bool sortPartial() const { return sortPartial_; }
 private:
+    bool sortPartial_{};
```

`include/glancelot/directory_listing.hpp`: `DirectoryListing` gains `bool sortPartial{false}`. If A1 has changed these signatures by then, A2 appends to A1's form. PR 7 (A1, not merged at this freeze) moves the grid from `listDirectory` to a scan in `src/app/directory_scan.hpp`, so once it lands the grid path means that scan: it reads the same default reader, and A2 adds `sortPartial` to its result. Its `ScanOrder` already sends these two keys through `fileSortBefore`, so they keep natural order until A2. `Navigator` is constructed fresh at several controller sites (`viewer_controller.cpp:1197`, `:1341`, `:1838`, `:1953`, `:1966`), which is why the reader is process-wide: a per-instance setter would silently give natural order at any site it missed.

`include/glancelot/directory_entry_model.hpp`:

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
+    // previous window: queued rows outside it are cancelled. Only image rows are read; others report metaState
+    // "unavailable" without I/O.
+    static constexpr int kMaxMetadataWindow = 512;
+    Q_INVOKABLE void requestMetadata(int firstRow, int lastRow);
+    // Whole-folder key pass with no reorder: the order path's worker, budget, cancellation and fencing. Rows become ready through
+    // the same dataChanged batching. Idempotent per listing generation; cleared by setDirectory, setActive(false) and a new listing.
+    Q_INVOKABLE void requestAllMetadata();
+    Q_PROPERTY(bool metadataPending READ metadataPending NOTIFY sortStateChanged)
+    Q_PROPERTY(bool metadataPartial READ metadataPartial NOTIFY sortStateChanged)  // budget ended; unread rows stay unknown
+    bool sortPending() const; bool sortPartial() const; bool metadataPending() const; bool metadataPartial() const;
 signals:
+    void sortStateChanged();
```

Role names, added to `DirectoryEntryModel::roleNames` (`directory_entry_model.cpp:454`): `captureTime` (QDateTime, `Qt::LocalTime` built from the wall-clock fields), `pixelWidth`, `pixelHeight` (int), `rating` (int), `metaState` (`"unrequested" | "pending" | "ready" | "unavailable" | "failed"`).

New `src/app/metadata_keys.hpp` (Track A owned, not a shared header):

```cpp
#pragma once
#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
namespace glancelot {
enum class MetaState : std::uint8_t { Unrequested, Pending, Ready, Unavailable, Failed };
// Lightweight per-file keys, not the HUD's MetadataRecord. Parsed from at most kMaxReadBytes of the file header
// with a direct JPEG/TIFF/PNG/WebP header reader. No QImageReader, no pixel decode, never on the GUI thread.
struct MetaKeys {
    static constexpr qint64 kMaxReadBytes = 64 * 1024;
    // EXIF DateTimeOriginal (+SubSecTimeOriginal) as wall-clock ms on a fixed epoch, ignoring OffsetTimeOriginal
    // for ordering. No fallback to file modified time.
    std::optional<std::int64_t> captureTime;
    std::optional<std::int16_t> captureOffsetMinutes;   // display only, when OffsetTimeOriginal exists
    std::uint32_t pixelWidth{}, pixelHeight{};          // display-oriented (EXIF orientation applied); 0 = unknown
    // -1 = rejected, 0 = explicitly unrated, 1..5 stars; nullopt = no rating found. Embedded EXIF 0x4746 or the
    // embedded XMP packet's xmp:Rating, overridden by the sidecar overlay when one is installed.
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
// Sidecar rating (C1). Installed once at startup, called on the reading worker only when ratings are wanted (the display path and
// requestAllMetadata, not a captured or dimensions sort), because it does sidecar I/O outside the per-file bound. nullopt keeps the
// embedded value. A cache entry read without the overlay counts as a miss for rating, so a later display read still applies it.
using RatingOverlay = std::function<std::optional<std::int8_t>(const QString& imagePath)>;
void setRatingOverlay(RatingOverlay overlay);
// Worker-thread reader used by both paths and installed as the SortKeyReader.
MetaKeys readMetaKeys(const QString& path, std::stop_token cancel);
}
```

## Sort semantics (`fileSortBefore`)

- `Captured`: `capturedKnown` first in both directions, then `captured`, then natural name.
- `Dimensions`: `dimensionsKnown` first in both directions, then `pixelWidth*pixelHeight` (64-bit), then `pixelWidth`, then natural name.
- An empty or failing reader makes every key missing, so the result equals ascending natural order in both directions. Shuffle, Name, Modified and Size are unchanged byte for byte.
- `needMeta` in `sortedImageFiles` (`navigation.cpp:241`) becomes `Modified || Size || Captured || Dimensions`, so the reader receives each file's stat data (size and modified), which it needs as the cache revision. The reader is then called once over the listed files before sorting. Name and Shuffle still skip stat.
- **One pass per folder and revision.** The grid and navigation workers share a pass: the first spends the budget and records the pass in `MetadataKeyCache` (A2 adds that call), and the other reads the cache only. A new listing's key pass resumes from the cache, so a large folder on slow storage fills in across visits. So their unread sets, and `sortPartial`, agree, and traversal matches the grid even when partial.
- **Arrival (A1).** While a captured or dimensions sort reads its keys, rows stay in the order they arrived. `arriving` (PR 7) and `sortPending` stay true until the keys are in, and the definitive order is published once.
- **Crossing a folder under these sorts.** There is no previous order to keep, so the landing waits for the pass or its budget, cancellable, with `navigating` showing (PR 3).
- Capture time orders by the wall-clock value the camera recorded. The offset is shown, never used for ordering.

## Threading, fencing and bounds

- **GUI thread:** `DirectoryEntryModel` and its role storage. `data()` never reads a file. It returns what is stored for the row, or undefined plus `metaState`.
- **Workers:** the display path uses a second `QThreadPool` in the model, separate from the listing pool (`kMaxConcurrentListings`, `directory_entry_model.cpp:27`), with at most `IoScheduler::maxConcurrentBackgroundWorkers(directory)` threads and never more than 2. The order path runs inside the existing listing or navigation job. Each file read is admitted through `glancelot::detail::admitIoJob(JobPriority::Metadata, kMaxReadBytes, path)` (`src/app/io_job_lease.hpp`). `tryAdmit` refuses non-foreground work while the background slots are busy, so a worker pass waits for admission with cancellation, and that wait counts against the folder budget. While the grid is visible on storage with two background slots, one stays free for thumbnails.
- **Fencing:**
  - each `requestMetadata` call increments `metaGeneration`;
  - `setDirectory`, `setActive(false)`, `setSort`, `setIncludeHidden` and a listing apply all increment it and request a stop;
  - a display-path result is applied only when its `metaGeneration` and listing `generation` both match (a key-pass result needs only the listing `generation`, so scrolling can't discard the pass), and only to the row found by **path key** at apply time, never by the row index captured at request time;
  - a result whose (size, modified) differs from the row's is discarded, and the row returns to `unrequested`.
- **Bounds:**
  - display queue: at most `kMaxMetadataWindow` pending rows, with at most 64 rows per `dataChanged` emission, coalesced on a 16 ms timer so GUI work per tick stays bounded;
  - per-file read: `kMaxReadBytes`;
  - folder budget for the order path and the key pass: 20,000 image files, or 4 s of read time on any storage that isn't known to be an SSD (network, rotational or unknown), both named constants that only the lead may change;
  - cache: `kMaxEntries`.
- **Truncated listings:** sorting covers only the listed subset; `truncated` already says so.
- **Parser trust:** the reader parses untrusted headers in-process on a worker thread, as `MetadataParser::parse` does. F4 must include it as a fuzz target.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Admission refused | The row stays `pending` and is retried on `IoScheduler::admissionAvailable` |
| Read error, unsupported format, truncated or cyclic metadata | `metaState` is `failed` or `unavailable`; the `*Known` flag stays false; the file sorts last; nothing throws across the worker boundary |
| Cancel during the order pass | Cooperative through `std::stop_token` between files (a blocked OS read cannot be interrupted, the same limit as listing). No order is published and the old order stays |
| Folder budget exhausted | Order path: publish with unread files last and `sortPartial = true`. Key pass: unread rows stay unknown and `metadataPartial = true` |
| File changed during a read | The stale key is discarded |
| Listing replaced mid-pass | The stale result is dropped by generation |
| Reader absent (tests, or a core-only build) | Every key is missing, so natural order |

## Migration without behaviour change

| Site | A2's change |
| --- | --- |
| `navigation.cpp` `sortBefore` | Replace the two-key natural-order return with the semantics above. The old keys go through the same code. |
| `viewer_controller.cpp:591-595` (settings parse), `:1987-1991` (`setSortKey`) | Accept both strings. Unknown strings still fall back to Name, so older builds read new settings as Name. |
| `viewer_controller.cpp:1715` (`sort` command cycle) | **Unchanged**: four keys. The new keys come from the sort menu and Settings only. |
| `settings_policy.cpp:140` (`sort.key` choice) | Accept both, or a saved new key would fail settings validation. |
| `directory_entry_model.cpp:64` `sortSpec()` | Accept both. |
| `ui/SettingsDialog.qml:637-638`, `ui/Main.qml:1827` | Add both names. A2's brief grants these lines. |
| App startup | The app calls `setDefaultSortKeyReader(...)` once, before any worker runs. Tests without it keep current behaviour. |
| `FileMeta{a,b,c}` (`directory_listing.cpp:78,114`, `navigation.cpp:102`) | Compile unchanged. `FileMeta` grows from 24 to 48 bytes, about 2.4 MB more at the 100,000-entry cap. |
| `MetadataParser::parse` and the HUD | Untouched. The HUD keeps its full record; grid keys never go through it. |
| Roles | Appended after `CurrentRole`; existing role numbers and names unchanged. |

With no QML calling `requestMetadata` and no new sort selected, behaviour and I/O are identical to today.

## Acceptance tests A2 must pass

Generated media only. New `tests/meta_roles_tests.cpp` (core, no display) and `tests/meta_roles_ui_tests.cpp` (model plus QML grid), through a lead-provided `cmake/MetaRolesTests.cmake`.

- **Reader:** generated JPEGs with DateTimeOriginal, SubSec and Offset; no EXIF; orientation 6 (swapped dimensions); PNG IHDR; TIFF; a truncated APP1; a cyclic IFD; a 100 KiB APP1 beyond the read cap; EXIF Rating 0x4746 values -1, 0, 3 and 5; an XMP packet rating. Each case asserts the keys or the missing flags, and that nothing reads past `kMaxReadBytes`.
- **Sort:**
  - ascending and descending, with missing values last in both directions;
  - equal keys break ties by natural name;
  - an empty reader equals ascending natural order;
  - traversal (`Navigator::step`), `sortedImageFiles`, `listDirectory` and the grid's own listing path produce the same image order for the same folder.
- **Settings:** a saved `captured` key survives `settings_policy` validation and a restart; a build that does not know the key falls back to Name.
- **Parity across construction sites:** under `captured`, a step that crosses a folder boundary, a lookahead and a folder jump all use the installed reader, with no site falling back to natural order.
- **Order path:**
  - cancel mid-pass (no publication, old order kept);
  - folder budget exhausted (`sortPartial`, unread files last);
  - a file changed during the pass (stale key discarded);
  - a listing replaced mid-pass by a newer generation (stale result dropped);
  - admission held busy, then released: the pass waits, then continues, and the wait counts against the budget;
  - the grid and traversal read one folder at once: one pass, the same unread set and the same order when partial.
- **Key pass:**
  - `requestAllMetadata` leaves the order unchanged under every sort and fills off-screen rows;
  - scrolling during the pass discards nothing;
  - a new listing resumes from the cache after a partial pass;
  - a second call in one listing generation starts nothing;
  - `setDirectory` and `setActive(false)` cancel it;
  - the budget reports `metadataPartial`, and unread rows stay unknown;
  - a chip filter and a rating group settle once the pass ends.
- **Display path:**
  - window clamped to 512 rows;
  - scroll storm: 50 windows in a row leave at most 512 queued and only the last window applied;
  - a result for a row that moved, by an inserted file, lands on the right path;
  - a result after `setDirectory` is dropped;
  - `setActive(false)` cancels;
  - admission refused leaves the row `pending`, then fills after release;
  - a non-image row is `unavailable` with no I/O (counted through a reader hook);
  - no `data()` call performs I/O (hook counter stays 0 while delegates scroll).
- **Overlay:** an installed overlay overrides an embedded rating; a cleared overlay value keeps it; `invalidate` makes the next read see a new sidecar value; a `captured` sort pass doesn't call it, and a display read afterwards shows the sidecar's rating, not the embedded one.
- **Budget:** 10k generated rows, with `requestMetadata` GUI work per tick under 8 ms (label `budget`).

## Not covered

- Writing ratings, labels or picks (C1, C2), and sidecar parsing rules (C1 implements the overlay).
- Grouping, header rows and collapse (A3); chips and their counts (A4).
- Incremental listing and batched arrival (A1). This contract assumes A1's "definitive order published once" and adds only the key pass.
- HUD metadata (`MetadataModel`, `MetadataParser::parse`); its bit depth and colour space stay as they are (see CAPABILITY).
- Persisting metadata to disk; folder order (always natural, NAV-02); filters on metadata values.
