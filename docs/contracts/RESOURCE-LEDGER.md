# RESOURCE-LEDGER contract

Status: **frozen** by PHASE2-CONTRACTS. Header: [`include/glancelot/resource_ledger.hpp`](../../include/glancelot/resource_ledger.hpp), the authoritative text of the interface. Code references are at `b1d57cd`; where PR 3 (`claude/stack-confirm-fix`) matters, it is named. Owner: Track E. Implementing packet: E1 (PERF-RESOURCE). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md).

## Purpose

One admission contract for every byte the viewer allocates on behalf of images: **reserve, then own, then release exactly once**. It replaces independent budgets that each believe they own the machine:

| Today | Where | Budget |
| --- | --- | --- |
| Presentation retained pixels | `DisplayManager::Private::enforceBudget`, `retainedBytes`, `s_imageBudgetBytes` (`display_manager.cpp:27`, `:193`, `:209`) | 256 MiB across six slots: current, pinned, comparison, previous, pending and the F040 reference, deduplicated by `QImage::cacheKey()` |
| GPU textures | `CanvasTextureAdmission` (`src/render/canvas_texture_admission.hpp`), shared per canvas group; `kPresentationTextureBudget`, `kPresentationColorReservation` (`presentation_window.hpp:25`, `:28`); `defaultTextureBudget` (`image_canvas.cpp:23`) | 256 MiB per group (split 1/2 in presentation comparison, `presentation_window.cpp:128-132`), 16 MiB colour |
| I/O admission | `IoScheduler::tryAdmit`, `memoryLimit_`, `admittedBytes_` (`io_scheduler.cpp:103-136`) | 256 MiB of in-flight job estimates |
| Main decode allowance | `viewer_controller.cpp:1020` (and `:727` for prefetch), `phase=="probe"` | `bytes*4 + current + cache <= min(1 GiB, 40% of available physical)` (the 40% applies on Windows only) |
| Decoded cache | `glancelot::detail::DecodeCache` (`src/app/decode_cache.hpp`) | 128 MiB |
| Thumbnail cache | `ThumbnailService::kCacheBytes` (`thumbnail_service.hpp:22`) | 64 MiB |

## Consumers

**E1** owns and migrates. **A5** (thumbnail buckets), **C3** (every compare pane), **D1** (retained tiers), **D3** (texture and staging reuse), **D5** (tile ceiling), **E2/E3** (cache controls, power profiles show effective limits) and **F1** (container cgroup limits) adopt.

## Frozen interface

The header is frozen as committed: `Ledger`, `Priority`, `Pressure`, `Refusal`, `Enforcement`, the `k*` constants, `LimitSources`, `effectiveLimit`, `ProbeOptions`, `probeSystemLimits`, `Thresholds`, `OwnerId`, `Request`, `Reservation` and `ResourceLedger`. Nothing is implemented until E1, and nothing but the compile test includes it.

## Semantics

**Admission order** in `tryReserve` (first failure wins, nothing changes on refusal):

1. Validate: registered owner, `bytes >= 0`, no `qint64` overflow, live count `< kMaxLiveReservations`, not shut down.
2. **Share hit**: if `shareKey != 0` and an owned entry with the same key exists, the handle is a share: `charged() == 0`, `bytes()` is the full size. It is always admitted for the aggregate, but still counts toward the owner quota.
3. Owner quota (`ownerBytes + bytes`, where owner bytes include shares, deduplicated per owner).
4. Ledger ceiling (physical, deduplicated).
5. Aggregate (only ledgers marked as aggregate members) `<= effectiveLimit`. Non-Foreground requests must also leave the foreground headroom free. At `Elevated`, Speculative requests are refused (`PressureShed`). At `Critical`, Background and Speculative are refused. Foreground ignores pressure but never exceeds the limit.

**Foreground headroom.** `min(kMaxForegroundHeadroomBytes, effectiveLimit / 2)`, recomputed whenever the limit changes. `kMaxForegroundHeadroomBytes` is one decode at the largest validated size (256 MiB of pixels plus 2x scratch). The cap of half the limit keeps background work alive on small machines. A power profile (E3) may lower it, never raise it.

**Charge attribution for shared storage.** One physical buffer is charged once, to the most essential ledger among its holders (`CpuPixels > GpuTextures > RetainedCache > Thumbnails > Transfer > WorkerScratch`). When that holder releases, the charge moves to the next holder under the same lock, so totals never dip and double. A `QImage::detach()` or `copy()` is a new buffer and needs a new reservation.

**Ownership and threading.** A `Reservation` is a value. Whoever holds it owns the bytes and may move it across threads (into a `QProcess` retirement closure, into a render-thread renderer). The ledger lock is never held while it calls user code. Signals are queued to the ledger's (GUI) thread. Reclaimers run there only.

**Owner granularity.** One owner per service and per canvas group, plus `compare:<slot>` per window (COMPARE-PANES). Per-pane attribution comes from `Request::tag`. `kMaxOwners` covers this.

**Diagnostics.** `Request::tag` feeds `snapshot()` only. The ledger never calls `perf_trace`, because `tryReserve` runs under a lock on any thread. E2 fixes the misleading `cache_lookup` span (`viewer_controller.cpp:944`) separately.

**Generation fencing.** The ledger does not decide staleness. Owners fence results with their existing generations (selection generation, thumbnail epoch, `workerGeneration`). The contract only guarantees that a stale or late completion's reservation is released, never committed into a newer owner. Owners call `commit` only after their own generation check passes, and release otherwise.

**Pressure with hysteresis.** `ratio = aggregate / effectiveLimit`. Normal goes to Elevated at `reclaimAt`, and to Critical at `criticalAt` or when an external signal says so. The level drops only below `recoverAt`, and only after `cooldownMs` since the last rise. On each rise the ledger asks reclaimers, tier by tier, for `aggregate - recoverAt*limit` bytes. Order (PERFORMANCE-STRATEGY section 5): Speculative (prefetch and decoded-cache entries that are not current), Hidden (thumbnails of hidden panes, textures of inactive windows, presentation `previous`), Background (paused metadata and prefetch, presentation comparison), Quality (labelled lower fit tier, after D1). The current image and the last valid frame of every visible pane, including the reference window, are never reclaimed. The 80/65 % values are tuning hypotheses, not shipped defaults.

**LimitProbe** (`src/app/resource_limit_probe.*`, E1). Runs on a worker. It samples every 2 s while work is active and every 10 s when idle, plus on demand after a refusal. Windows: `GlobalMemoryStatusEx` x 0.40 (today's rule). Linux: `MemAvailable` x 0.40; the `/proc/self/cgroup` path walked to the root, minimum `memory.max`; cgroup v1 `memory.limit_in_bytes` fallback; `max` or unreadable values become `kUnknown`. It is never polled on the hot path.

**Window processes.** Each window slot is its own process with its own ledger (`window_slot.hpp`). Two windows can together exceed one window's limit. This is a documented gap, revisited after B8 single-instance routing.

**GPU membership.** `GpuTextures` is outside the aggregate in M0 and M1. In M2 it joins when the adapter reports unified memory or the memory type is unknown, because VRAM is not a separate pool there.

**Transfer segment lifetime (PR 3).** After PR 3 the decoder transfer segment is `glancelot::detail::TransferPtr`: a segment is destroyed on a short-lived reaper thread, and beyond `MaxPendingSharedMemoryReaps` (16) it is deliberately leaked (`src/app/shared_memory_reaper.hpp`). The Transfer reservation lives with the segment, so its bytes leave the ledger only after the segment is really gone:

- E1 moves the Transfer `Reservation` into the reaper's closure and releases it after `delete memory`;
- a reap blocked on a dead worker's semaphore stays charged while it blocks, which is true: the memory is still mapped;
- a leaked segment stays charged for the life of the process, with its `Reservation` parked in a process-lifetime holder, and `leakedSharedMemorySegments()` appears in E2's diagnostics.

E1 must not start before PR 3 is merged.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Any `tryReserve` refusal | An empty `Reservation` with `refusal()` set; nothing changed. The caller reports or defers, and no call blocks |
| Worker killed after `probe` | Scratch is released only once `QProcess` reaches NotRunning (`retireIoProcess`, `src/app/io_job_lease.hpp`), so totals never undercount a process that is still running |
| Cancel during transfer | Transfer released exactly once, CpuPixels never committed |
| Stale completion | The stale handle is released, not committed; the newer reservation and `ownerBytes` are unaffected |
| A reclaimer that reserves | Rejected. Reclaimers run outside the lock, in tier order |
| Device loss | The renderer `reset()` releases texture reservations; recovery re-reserves within quota, never resurrecting an obsolete generation |
| `shutdown()` with work active | New reservations are refused with `ShuttingDown`; in-flight work releases normally and the live count reaches 0 |
| `kMaxOwners` reached | `registerOwner` returns an empty `OwnerId`; every `tryReserve` with it is `InvalidRequest` |

## Migration without behaviour change

Three steps, each a separately gated commit inside E1:

- **M0 Shadow.** The ledger exists with `Enforcement::Shadow`. Every site below reserves and releases, but the existing checks still decide. Tests assert ledger totals equal the legacy counters, and that the live count is zero at teardown.
- **M1 Enforce, same numbers.** The ceilings and quotas below reproduce today's limits exactly. The legacy counters become read-only views over the ledger.
- **M2 Aggregate.** `LimitSources` and pressure are enabled, the Linux/cgroup probe goes live and the decode allowance is removed. This step changes behaviour on purpose and is measured.

| Existing budget | Ledger mapping (M1) | Why nothing is double counted |
| --- | --- | --- |
| Decode probe `bytes*4 + current + cache` | WorkerScratch `2*bytes` (decoder source plus converted, released when `retireIoProcess` disposes); Transfer `bytes` (the segment lifetime above); CpuPixels `bytes` (parent copy, committed at `decoded` with `shareKey = image.cacheKey()`). `current + cache` come from ledger totals | The 4x estimate is split into three real lifetimes instead of being added to separately counted pixels. In M1 the allowance comparison keeps reading `min(1 GiB, 40%)` as a ceiling over these ledgers |
| `IoScheduler` 256 MiB `admittedBytes_` | Transfer ceiling 256 MiB. `IoJobLease` holds a `Reservation` next to its job id. `IoScheduler` keeps job counts, the foreground/prefetch exclusion and storage-type concurrency; `memoryLimit()` and `admittedBytes()` become views (`setMemoryLimit` forwards to `setLedgerCeiling(Transfer)`) | Bytes live only in the ledger. The scheduler counts jobs, not bytes. The "sole foreground job may exceed the limit" exception stays in `tryAdmit` until M2 (the probe already caps one image at 256 MiB) |
| `DisplayManager` 256 MiB retained | One OwnerId `presentation`, CpuPixels owner quota 256 MiB. Each of the six slots holds a `Reservation` with `shareKey = cacheKey()`. `enforceBudget` keeps its shedding order and the F040 transactional admission (`setReferenceImage`, `display_manager.cpp:727`), and runs on refusal and from Hidden/Background reclaimers | `current` shares the controller's CpuPixels charge (zero physical charge). The owner quota still counts it, so `enforceBudget` behaves exactly as today. `referenceShed` still fires when the reference is released for a newer frame |
| Decode cache 128 MiB | RetainedCache ceiling 128 MiB, owner `decoded-cache`, Speculative reclaimer (replaces the `cache.clear()` in the probe, `viewer_controller.cpp:1021`) | The current image and its cache entry share one buffer, charged to CpuPixels while it is current |
| Thumbnail `QCache` 64 MiB plus worker estimate | Thumbnails ceiling 64 MiB (cache entries); WorkerScratch for the in-flight estimate via `IoJobLease` | Separate physical buffers |
| `CanvasTextureAdmission` | GpuTextures. One OwnerId per canvas group (main canvas, F040 reference canvas, presentation window), owner quota = `aggregateBudget`. Colour atlas 16 MiB as a fixed owned reservation. `CanvasTextureAdmission` keeps `reserve(bytes,limit)` and `release(bytes)` and holds `Reservation`s internally, so render-thread call sites and the bounded retry stay unchanged | GpuTextures is not an aggregate member in M1 |

## Acceptance tests E1 must pass

`tests/resource_ledger_tests.cpp`, through a lead-provided `cmake/ResourceLedgerTests.cmake`.

- **Unit:** refusal precedence; overflow and negative sizes; `commit` shrink returns the difference; double `release`, move-assign and destructor release once (counter checked); `kMaxLiveReservations`; share hit charges once and the charge migrates on release; per-owner dedupe; `effectiveLimit` with every combination of unknown sources; foreground headroom with a limit above and below twice `kMaxForegroundHeadroomBytes`; hysteresis (rise, no fall above `recoverAt`, fall only after cooldown); reclaimers run outside the lock in tier order and a reclaimer that reserves is rejected.
- **Probe:** fake `/proc` and `/sys/fs/cgroup` trees: v2 nested ancestors (the smallest wins), `max`, v1, unreadable controller gives `kUnknown`, all unknown gives the fallback.
- **Crash after reserve:** the decoder worker is killed after `probe` (Transfer and Scratch reserved). Scratch is released only once `QProcess` reaches NotRunning, and the totals return to baseline. Same for a thumbnail worker.
- **Cancel during transfer:** cancel between `create(bytes)` and `decoded`. The Transfer reservation is released exactly once and CpuPixels is never committed.
- **Reaper:** a reap held open by a test hook keeps Transfer charged, and releasing the hook returns it to baseline. With the cap reached, the leaked segment stays charged and is visible in the snapshot and in `leakedSharedMemorySegments()`.
- **Late completion:** a stale `decoded` arrives after a newer selection. The stale handle is released, not committed. The newer reservation is unaffected and `ownerBytes` is unchanged.
- **Two windows, same source:** main canvas, F040 reference and presentation show the same `QImage`. CpuPixels is charged once, GpuTextures once per canvas group. Closing each window in any order returns to baseline. Two `glancelot` processes are accounted independently.
- **F040 parity:** `referenceShed` and the pin refusals fire at exactly the same budgets as before M1.
- **Device loss:** the renderer `reset()` releases texture reservations, and recovery re-reserves without exceeding the quota.
- **Shutdown with active I/O:** `shutdown()` refuses new reservations, in-flight work releases normally, and the live count reaches 0.
- **Parity (M0):** the existing `cache`, `io_scheduler`, `reference_pin` and presentation budget tests pass unchanged, and the shadow totals equal the legacy counters.
- **Measured peaks (M2):** working-set high-water mark next to the ledger high-water mark in the soak (F4). Codec-internal allocations are not visible to accounting, and the handoff says so.

## Not covered

- No cross-process coordinator, shared memory segment or IPC in this contract.
- No accounting of codec-internal or Qt scene-graph allocations beyond fixed reservations. Measured peaks cover them.
- The ledger does not change cache policies (LRU, lookahead). It does not schedule I/O and does not replace `IoScheduler` concurrency.
- No raising of any cap, and no change to dimension, overflow or parser validation.
- No user-visible settings. E2 and E3 add them on top.
