# RESOURCE-LEDGER contract proposal (draft for the lead)

Status: **draft, not frozen**. Proposed header: `include/glancelot/resource_ledger.hpp`. Base: `origin/main` at `3893c77`.

## 1. Purpose and consumers

One admission contract for every byte the viewer allocates on behalf of images: **reserve, then own, then release exactly once**. It replaces three independent budgets that each believe they own the machine:

| Today | Where | Budget |
| --- | --- | --- |
| Presentation retained pixels | `DisplayManager::Private::enforceBudget`, `s_imageBudgetBytes` | 256 MiB across current/pinned/comparison/previous/pending, deduplicated by `QImage::cacheKey()` |
| GPU textures | `CanvasTextureAdmission` shared per canvas group; `kPresentationTextureBudget`, `kPresentationColorReservation` | 256 MiB per group (split 1/2 in presentation comparison), 16 MiB colour |
| I/O admission | `IoScheduler::tryAdmit`/`memoryLimit_` | 256 MiB of in-flight job estimates |
| Main decode allowance | `viewer_controller.cpp` probe phase | `bytes*4 + current + cache <= min(1 GiB, 40% of available physical)` (Windows only) |
| Decoded cache | `ViewerController` `QCache` | 128 MiB |
| Thumbnail cache | `ThumbnailService::kCacheBytes` | 64 MiB |

Consumers (LEAD-PLAN section 6): **E1** owns and migrates; **A5** (thumbnail buckets), **C3** (every compare pane), **D3** (texture/staging reuse), **D5** (tile ceiling), **E2/E3** (cache controls, power profiles show effective limits), **F1** (container cgroup limits) adopt.

## 2. Proposed header

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <array>
#include <functional>
#include <memory>

namespace glancelot::resource {
// Lead-frozen RESOURCE-LEDGER. One process-wide ledger; every image-sized allocation is reserved
// before it is made, committed when it exists and released exactly once on success, cancel,
// worker death, stale completion or shutdown. No stable external plugin ABI.
enum class Ledger : quint8 { CpuPixels, WorkerScratch, Transfer, RetainedCache, Thumbnails, GpuTextures };
inline constexpr int kLedgerCount = 6;
enum class Priority : quint8 { Foreground, Visible, Background, Speculative };
enum class Pressure : quint8 { Normal, Elevated, Critical };
enum class Refusal : quint8 { None, InvalidRequest, OwnerQuota, LedgerCeiling, AggregateLimit,
                              PressureShed, TooManyReservations, ShuttingDown };
enum class Enforcement : quint8 { Shadow, Enforce }; // Shadow: always admit, count would-refuse

inline constexpr qint64 kUnknown = -1;               // unreadable/unlimited source; never treated as zero
inline constexpr int kMaxLiveReservations = 4096;
inline constexpr int kMaxOwners = 64;

struct LimitSources {
    qint64 userCapBytes{kUnknown};      // Settings or power profile (E3)
    qint64 osHeadroomBytes{kUnknown};   // safe fraction of OS-available physical memory
    qint64 cgroupLimitBytes{kUnknown};  // min over ancestors: memory.max (v2) / limit_in_bytes (v1)
    qint64 overheadBytes{0};            // measured non-image process overhead
    qint64 safetyReserveBytes{64LL << 20};
    qint64 fallbackBytes{1LL << 30};    // when user, OS and cgroup are all unknown
};
// min(known of user, OS, cgroup) - overhead - safetyReserve, floored at 0; fallbackBytes if none known.
qint64 effectiveLimit(const LimitSources&) noexcept;

struct ProbeOptions { QString procRoot{"/proc"}, cgroupRoot{"/sys/fs/cgroup"}; double osFraction{0.40}; };
// Blocking file reads: worker thread only (LimitProbe), never UI or render thread. Fake roots in tests.
LimitSources probeSystemLimits(const ProbeOptions& = {});

struct Thresholds { double reclaimAt{0.80}, recoverAt{0.65}, criticalAt{0.95}; int cooldownMs{2000}; };

struct OwnerId {
    quint32 value{};
    explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const OwnerId&) const = default;
};

struct Request {
    Ledger ledger{Ledger::CpuPixels};
    Priority priority{Priority::Background};
    OwnerId owner;
    qint64 bytes{};           // checked stride*height (+ staging); >= 0, overflow checked by the caller
    quint64 shareKey{};       // nonzero: physical storage identity (cacheKey of the detached buffer)
    quint64 generation{};     // caller's generation; diagnostics only, the ledger never judges staleness
    const char* tag{""};      // static string for perf_trace and diagnostics
};

struct ReservationState;
// Move-only. Empty -> Reserved -> Owned -> Released. release() and the destructor are idempotent and
// thread-safe; the bytes leave the ledger exactly once whichever path gets there first.
class Reservation {
public:
    Reservation() = default;
    Reservation(Reservation&&) noexcept;
    Reservation& operator=(Reservation&&) noexcept;   // releases the held reservation first
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    ~Reservation();
    explicit operator bool() const noexcept;          // Reserved or Owned
    bool owned() const noexcept;
    qint64 bytes() const noexcept;                    // what the owner holds (its quota view)
    qint64 charged() const noexcept;                  // what this handle adds to physical totals (0 for a share)
    Ledger ledger() const noexcept;
    Refusal refusal() const noexcept;                 // reason when empty after tryReserve
    // Publication. actualBytes <= bytes(); the difference returns at once. Growing needs a new
    // reservation. False and no change when empty, released or already owned.
    bool commit(qint64 actualBytes);
    void release() noexcept;
private:
    friend class ResourceLedger;
    std::shared_ptr<ReservationState> state_;
};

class ResourceLedger final : public QObject {
    Q_OBJECT
public:
    static ResourceLedger& instance();                // created on the GUI thread before any window
    OwnerId registerOwner(const QString& diagnosticName);           // 0 when kMaxOwners reached
    void unregisterOwner(OwnerId);                    // owner must have no live reservations (tested)
    void setOwnerQuota(OwnerId, Ledger, qint64 bytes);              // kUnknown = no per-owner quota
    void setLedgerCeiling(Ledger, qint64 bytes);                    // kUnknown = aggregate only
    void setAggregateMember(Ledger, bool counts);     // GpuTextures: false until unified memory is decided
    void setForegroundHeadroom(qint64 bytes);         // kept free from non-Foreground requests
    void setLimitSources(const LimitSources&);
    void setThresholds(const Thresholds&);
    void setEnforcement(Enforcement);
    void notifyExternalPressure(Pressure);            // OS signal (Windows notification, PSI, memory.events)

    // Never blocks or waits; O(1) under one mutex; any thread, including scene-graph render threads.
    Reservation tryReserve(const Request&);

    struct LedgerSnapshot { qint64 charged{}, ceiling{kUnknown}, highWater{}; int live{}; quint64 refusals{}, wouldRefuse{}; };
    struct Snapshot {
        std::array<LedgerSnapshot, kLedgerCount> ledgers;
        qint64 effectiveLimit{}, aggregate{}, highWater{};
        Pressure pressure{Pressure::Normal};
        quint64 revision{};                           // increments on every change; stale snapshots are dropped
    };
    Snapshot snapshot() const;
    qint64 ownerBytes(OwnerId, Ledger) const;

    enum class ReclaimTier : quint8 { Speculative, Hidden, Background, Quality };
    // Runs on the GUI thread, never under the ledger lock, cheapest tier first. Returns the bytes it
    // released through Reservation::release(); it must not allocate or reserve.
    using Reclaimer = std::function<qint64(qint64 targetBytes)>;
    quint64 addReclaimer(ReclaimTier, OwnerId, Reclaimer);
    void removeReclaimer(quint64 token);
    void shutdown();                                  // refuse new reservations; existing ones still release
signals:
    void pressureChanged(glancelot::resource::Pressure level);      // queued, hysteresis applied
    void capacityAvailable(glancelot::resource::Ledger ledger);     // coalesced, emitted outside the lock
private:
    ResourceLedger();
    struct Private;
    std::unique_ptr<Private> d;
};
}
```

## 3. Semantics

**Admission order** in `tryReserve` (first failure wins, nothing changes on refusal):
1. Validate: registered owner, `bytes >= 0`, no `qint64` overflow, live count `< kMaxLiveReservations`, not shut down.
2. **Share hit**: if `shareKey != 0` and an owned entry with the same key exists, the handle is a share: `charged() == 0`, `bytes()` is the full size. It is always admitted for the aggregate, but still counts toward the owner quota.
3. Owner quota (`ownerBytes + bytes`, where owner bytes include shares, deduplicated per owner).
4. Ledger ceiling (physical, deduplicated).
5. Aggregate (only ledgers marked as aggregate members) `<= effectiveLimit`. Non-Foreground requests must also leave `foregroundHeadroom` free. At `Elevated`, Speculative requests are refused (`PressureShed`). At `Critical`, Background and Speculative are refused. Foreground ignores pressure but never exceeds the limit.

**Charge attribution for shared storage.** One physical buffer is charged once, to the most essential ledger among its holders (`CpuPixels > GpuTextures > RetainedCache > Thumbnails > Transfer > WorkerScratch`). When that holder releases, the charge moves to the next holder under the same lock, so totals never dip and double. A `QImage::detach()`/`copy()` is a new buffer and needs a new reservation.

**Ownership and threading.** A `Reservation` is a value. Whoever holds it owns the bytes and may move it across threads (into a `QProcess` retirement closure, into a render-thread renderer). The ledger lock is never held while it calls user code. Signals are queued to the ledger's (GUI) thread. Reclaimers run there only.

**Generation fencing.** The ledger does not decide staleness. Owners fence results with their existing generations (selection generation, thumbnail epoch, `workerGeneration`). The contract only guarantees that a stale or late completion's reservation is released, never committed into a newer owner. Owners call `commit` only after their own generation check passes, and release otherwise.

**Pressure with hysteresis.** `ratio = aggregate / effectiveLimit`. Normal goes to Elevated at `reclaimAt`, and to Critical at `criticalAt` or when an external signal says so. The level drops only below `recoverAt`, and only after `cooldownMs` since the last rise. On each rise the ledger asks reclaimers, tier by tier, for `aggregate - recoverAt*limit` bytes. Order (PERFORMANCE-STRATEGY section 4): Speculative (prefetch and decoded-cache entries that are not current), Hidden (thumbnails of hidden panes, textures of inactive windows, presentation `previous`), Background (paused metadata and prefetch, presentation comparison), Quality (labelled lower fit tier, after D1). The current image and the last valid frame of every visible pane are never reclaimed. The 80/65 % values are tuning hypotheses, not shipped defaults.

**LimitProbe** (`src/app/resource_limit_probe.*`, E1). Runs on a worker. It samples every 2 s while work is active and every 10 s when idle, plus on demand after a refusal. Windows: `GlobalMemoryStatusEx` × 0.40 (today's rule). Linux: `MemAvailable` × 0.40; `/proc/self/cgroup` path walked to the root, minimum `memory.max`; cgroup v1 `memory.limit_in_bytes` fallback; `max`/unreadable values become `kUnknown`. It is never polled on the hot path.

## 4. Migration without behaviour change

Three steps, each a separately gated commit inside E1:

- **M0 Shadow.** The ledger exists with `Enforcement::Shadow`. Every site below reserves and releases, but the existing checks still decide. Tests assert ledger totals equal the legacy counters, and that the live count is zero at teardown.
- **M1 Enforce, same numbers.** The ceilings and quotas below reproduce today's limits exactly. The legacy counters become read-only views over the ledger.
- **M2 Aggregate.** `LimitSources` and pressure are enabled, the Linux/cgroup probe goes live and the decode allowance is removed. This step changes behaviour on purpose and is measured.

| Existing budget | Ledger mapping (M1) | Why nothing is double counted |
| --- | --- | --- |
| Decode probe `bytes*4 + current + cache` | WorkerScratch `2*bytes` (decoder source plus converted, released when `retireIoProcess` disposes); Transfer `bytes` (the `QSharedMemory` lifetime, released after copy/unlock/destroy); CpuPixels `bytes` (parent copy, committed at `decoded` with `shareKey = image.cacheKey()`). `current + cache` come from ledger totals | The 4× estimate is split into three real lifetimes instead of being added to separately counted pixels. In M1 the allowance comparison keeps reading `min(1 GiB, 40%)` as a ceiling over these ledgers |
| `IoScheduler` 256 MiB `admittedBytes_` | Transfer ceiling 256 MiB. `IoJobLease` holds a `Reservation` next to its job id. `IoScheduler` keeps job counts, the foreground/prefetch exclusion and storage-type concurrency; `memoryLimit()`/`admittedBytes()` become views (`setMemoryLimit` forwards to `setLedgerCeiling(Transfer)`) | Bytes live only in the ledger. The scheduler counts jobs, not bytes. The "sole foreground job may exceed the limit" exception stays in `tryAdmit` until M2 (the probe already caps one image at 256 MiB) |
| `DisplayManager` 256 MiB retained | One OwnerId `presentation`, CpuPixels owner quota 256 MiB. Each of the five slots holds a `Reservation` with `shareKey = cacheKey()`. `enforceBudget` keeps its shedding order and runs on refusal and from Hidden/Background reclaimers | `current` shares the controller's CpuPixels charge (zero physical charge). The owner quota still counts it, so `enforceBudget` behaves exactly as today |
| Controller `QCache` 128 MiB | RetainedCache ceiling 128 MiB, owner `decoded-cache`, Speculative reclaimer (replaces the `cache.clear()` in the probe) | The current image and its cache entry share one buffer, charged to CpuPixels while it is current |
| Thumbnail `QCache` 64 MiB plus worker estimate | Thumbnails ceiling 64 MiB (cache entries); WorkerScratch for the in-flight estimate via `IoJobLease` | Separate physical buffers |
| `CanvasTextureAdmission` | GpuTextures. One OwnerId per canvas group (main canvas, F040 reference canvas, presentation window), owner quota = `aggregateBudget`. Colour atlas 16 MiB as a fixed owned reservation. `CanvasTextureAdmission` keeps `reserve(bytes,limit)`/`release(bytes)` and holds `Reservation`s internally, so render-thread call sites and the bounded retry stay unchanged | GpuTextures is not an aggregate member in M1 (open question 2) |

## 5. Test plan (`tests/resource_ledger_tests.cpp`, `cmake/ResourceLedgerTests.cmake`)

- **Unit:** refusal precedence; overflow and negative sizes; `commit` shrink returns the difference; double `release`, move-assign and destructor release once (counter checked); `kMaxLiveReservations`; share hit charges once and the charge migrates on release; per-owner dedupe; `effectiveLimit` with every combination of unknown sources; hysteresis (rise, no fall above `recoverAt`, fall only after cooldown); reclaimers run outside the lock in tier order and a reclaimer that reserves is rejected.
- **Probe:** fake `/proc` and `/sys/fs/cgroup` trees: v2 nested ancestors (the smallest wins), `max`, v1, unreadable controller → `kUnknown`, all unknown → fallback.
- **Crash after reserve:** the decoder worker is killed after `probe` (Transfer and Scratch reserved). Scratch is released only once `QProcess` reaches NotRunning, and the totals return to baseline. Same for a thumbnail worker.
- **Cancel during transfer:** cancel between `create(bytes)` and `decoded`. The Transfer reservation is released exactly once and CpuPixels is never committed.
- **Late completion:** a stale `decoded` arrives after a newer selection. The stale handle is released, not committed. The newer reservation is unaffected and `ownerBytes` is unchanged.
- **Two windows, same source:** main canvas, F040 reference and presentation show the same `QImage`. CpuPixels is charged once, GpuTextures once per canvas group. Closing each window in any order returns to baseline. Separate processes: two `glancelot` processes are accounted independently (documented gap, open question 1).
- **Device loss:** the renderer `reset()` releases texture reservations, and recovery re-reserves without exceeding the quota.
- **Shutdown with active I/O:** `shutdown()` refuses new reservations, in-flight work releases normally, and the live count reaches 0.
- **Parity (M0):** existing `cache`, `io_scheduler`, `reference_pin` and presentation budget tests pass unchanged, and the shadow totals equal the legacy counters.
- **Measured peaks (M2):** working-set high-water mark next to the ledger high-water mark in the soak (F4). Codec-internal allocations are not visible to accounting, and the handoff says so.

## 6. Open questions for the lead

1. **Cross-process windows.** Every window is its own process (`WindowSlot`). Options: (a) keep per-process ledgers, a documented gap; (b) divide `userCap` by the live window count from `sessions.json`; (c) a local coordinator later. Proposal: (a) for E1; revisit after B8 single-instance routing.
2. **GPU in the aggregate.** On integrated and shared-memory GPUs, VRAM is not a separate pool. Proposal: aggregate membership off in M1; in M2, on when the adapter reports unified memory or the type is unknown.
3. **Foreground headroom default.** Proposal: the largest validated single decode (256 MiB pixels plus 2× scratch). Confirm, or tie it to the power profile.
4. **Owner granularity.** Is one owner per canvas group plus one per service enough for E2 diagnostics, or is per-pane attribution needed for C3?
5. Should `Request::tag` feed `perf_trace` spans (this fixes the misleading `cache_lookup` span in E2), or stay diagnostics-only?

## 7. Non-goals

- No cross-process coordinator, shared memory segment or IPC in this contract.
- No accounting of codec-internal or Qt scene-graph allocations beyond fixed reservations. Measured peaks cover them.
- The ledger does not change cache policies (LRU, lookahead). It does not schedule I/O and does not replace `IoScheduler` concurrency.
- No raising of any cap, and no change to dimension, overflow or parser validation.
- No user-visible settings. E2/E3 add them on top.
