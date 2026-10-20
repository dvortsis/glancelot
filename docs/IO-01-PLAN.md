# IO-01 Task Plan: Storage-Aware Admission Scheduler

- **Task ID**: `IO-01`
- **Requirements**: `PERF-05` (Storage-aware I/O concurrency), `SYS-02` (Robust resource and parser boundaries)
- **Milestone**: M3
- **Priority**: 8
- **Owner**: Gemini Lane (`agent/io`)
- **Worktree**: `.worktrees/io`
- **Build Directory**: `build-io`
- **Prerequisites**: `CACHE-01` (integrated at `8b28060`)

---

## 1. Context and Problem Statement

Storage media characteristics differ fundamentally across modern workstations:
- **Fast NVMe / SATA SSDs**: High random IOPS, near-zero seek latency. Can sustain parallel decoder processes (2-4 workers) without I/O head contention.
- **Rotational Hard Drives (HDDs)**: High seek penalty (~10-15 ms per random seek). Multiple concurrent random file reads cause disk thrashing, drastically reducing aggregate read bandwidth and causing foreground view latency stalls.
- **Network Shares (SMB, CIFS, NFS)**: Unpredictable round-trip latency, network congestion, and potential server-side rate limits. Excessive concurrent requests or aggressive lookahead prefetch can congest network pipes and delay immediate foreground image presentation.

Requirement `PERF-05` dictates:
> "Storage-aware I/O concurrency: SSD/HDD/SMB fixtures and manual override; adaptive metrics and cancellation; no UI blocking or decoder thread oversubscription."

Requirement `SYS-02` dictates:
> "Robust resource and parser boundaries: Bounded IPC/allocations, worker crash/deadline recovery, fuzz corpus, sandbox enforcement capability documented."

---

## 2. Architecture & Components

### 2.1 Storage Classification & Detection (`StorageDeviceType`)
The scheduler identifies the storage backend of any target file path:
- `StorageDeviceType::Ssd`: Solid-state storage (NVMe, SATA SSD).
- `StorageDeviceType::Rotational`: Mechanical hard drives (incurs seek penalty).
- `StorageDeviceType::Network`: Remote shares (UNC paths, SMB/CIFS, NFS).
- `StorageDeviceType::Unknown`: Default fallback.

#### Detection Mechanics:
- **Windows**:
  - UNC path detection (`\\` or `//`) maps immediately to `Network`.
  - Drive root query via `GetDriveTypeW`: `DRIVE_REMOTE` maps to `Network`.
  - Volume seek penalty query via `DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY, StorageDeviceSeekPenaltyProperty)`:
    - If `DEVICE_SEEK_PENALTY_DESCRIPTOR::IncursSeekPenalty == TRUE`, classifies as `Rotational`.
    - If `FALSE`, classifies as `Ssd`.
- **Linux / POSIX**:
  - Filesystem type inspection (`statfs` / `/proc/mounts`): CIFS, NFS, SMB classify as `Network`.
  - Sysfs queue attribute check (`/sys/block/<dev>/queue/rotational`): `1` classifies as `Rotational`, `0` classifies as `Ssd`.
- **Deterministic Test Fixtures**:
  - `setPathOverride(prefix, type)` allows automated tests to bind mock folders to SSD, HDD, or Network profiles without requiring physical multi-disk hardware.

### 2.2 Storage Profiles & Manual Overrides (`StorageProfile`)
Users can configure or force a storage profile:
- `StorageProfile::Auto`: Automatically detect device type.
- `StorageProfile::Ssd`: Force SSD concurrency rules.
- `StorageProfile::Rotational`: Force HDD serialized rules.
- `StorageProfile::Network`: Force Network conservative rules.

Persisted in `settings.json` under `performance.storageProfile` (or `storageProfile`) and remappable via controller commands.

### 2.3 Shared Admission & Concurrency Policies
`IoScheduler` regulates active jobs and memory allocations:

| Profile / Device | Max Background Workers | Lookahead Prefetch Allowed | Thumbnail Deadline | Concurrency Rule |
| :--- | :--- | :--- | :--- | :--- |
| **SSD** | Up to 2 (or max configured) | Yes (when idle) | 5,000 ms | Parallel background reads |
| **Rotational (HDD)** | 1 | Deferred / Throttled | 8,000 ms | Serialized background reads |
| **Network (SMB)** | 1 | Disabled / Strictly Opportunistic | 3,000 ms | Conservative; fast abort |

### 2.4 Priority Scheduling & Preemption
Four priority levels ensure interactive viewing responsiveness:
1. `JobPriority::Foreground` (Highest): User navigation (`open`, `next`, `previous`). Always admitted. Halts or defers speculative prefetch.
2. `JobPriority::Thumbnail` (Medium): Viewport thumbnails for filmstrip and grid.
3. `JobPriority::Prefetch` (Low): Lookahead into adjacent folders.
4. `JobPriority::Metadata` (Low): Background EXIF/IPTC parsing.

### 2.5 Bounded Combined Memory Ledger
- Total admitted bytes across all active jobs cannot exceed `maxAdmittedBytes` (system limit, default 256 MiB).
- Reservations are acquired before workers launch and released upon job completion or cancellation.
- Rejection prevents memory exhaustion and container OOM kills.

---

## 3. Interfaces & Contracts

Shared header: `include/glancelot/io_scheduler.hpp`
- `glancelot::IoScheduler` singleton interface.
- Detection, override, concurrency, and admission methods.
- Integrated into `ThumbnailService`, `ViewerController`, and settings.

---

## 4. Test Suite (`tests/io_scheduler_tests.cpp`)

1. `detectStorageType`: Validates SSD, Rotational, and Network classification.
2. `pathOverrides`: Path prefix fixture overrides for unit testing.
3. `concurrencyLimitsByProfile`: Concurrency clamping per storage profile.
4. `manualOverride`: Dynamic switching between Auto, SSD, HDD, and Network profiles.
5. `priorityAdmissionAndPreemption`: Foreground priority admission over background tasks.
6. `memoryBudgetCeiling`: Hard memory ceiling enforcement.
7. `thumbnailServiceIntegration`: Dynamic worker count adjustment on storage change.
8. `viewerControllerIntegration`: Controller commands, properties, and prefetch gating.
9. `cancellationReleasesAdmission`: Immediate release of admission slots on cancellation.
10. `settingsPersistence`: Round-trip persistence in `settings.json`.

---

## 5. Implementation & Verification Plan

1. **Lead Stage (main)**:
   - Commit `include/glancelot/io_scheduler.hpp`, `docs/IO-01-PLAN.md`, and update `CMakeLists.txt` & `docs/WORK-QUEUE.yaml`.
2. **Implementation Stage (`agent/io`)**:
   - Create `.worktrees/io`.
   - Implement `src/app/io_scheduler.cpp`.
   - Wire with `ThumbnailService`, `ViewerController`, and `SettingsFile`.
   - Implement `tests/io_scheduler_tests.cpp`.
   - Run Windows suite in `build-io` (24/24 tests passing).
3. **Review Stage**:
   - Independent verification report in `docs/verification/IO-01.md`.
4. **Integration Stage (main)**:
   - Acquire integration lock.
   - Merge `agent/io` into `main`.
   - Run root Windows test (`build.ps1 -Test`) and Linux Docker (`docker build`).
   - Record evidence, release lock, and commit.
