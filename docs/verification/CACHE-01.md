# CACHE-01 Independent Verification Report

- **Task**: CACHE-01 (Requirements PERF-04, NAV-07, SYS-02) — Revision-Validated Bidirectional Decoded Cache and Cancellable Cross-Folder Lookahead
- **Branch**: `agent/cache`
- **Review Workspace**: `C:\src\Github\Glancelot\.worktrees\cache`
- **Build Directory**: `build-cache`
- **Review Date**: 2026-10-19
- **Reviewer**: Independent Reviewer Agent (Codex/Claude Cross-Lane Review Protocol)
- **Status / Recommendation**: **APPROVED**

---

## 1. Executive Summary

Task `CACHE-01` delivers full compliance with requirements **PERF-04** (Decoded image cache), **NAV-07** (Continuous folder traversal with seamless crossing), and **SYS-02** (Bounded resource usage and decoder isolation) as specified in `docs/WORK-QUEUE.yaml`, `docs/CACHE-01-PLAN.md`, and `docs/REQUIREMENTS.md`.

### Key Features Implemented:
1. **Revision-Validated Bidirectional Decoded Cache (PERF-04)**:
   - Stores decoded image textures/surfaces keyed by canonical file path, file size, and file modification timestamp (`DecodedCacheKey`).
   - Path normalization via `QFileInfo::absoluteFilePath()` eliminates Windows forward-slash vs. backslash key divergence across navigation paths.
   - Integrated with `glancelot::perf::Flag::CacheHit` instrumentation for high-fidelity performance metrics.
   - Preserves explicit open semantics: explicit `open(path)` calls force a fresh disk decode to satisfy `AcceptanceTests::explicitOpenDoesNotReuseSameSizeTimestampPixels`, while navigation stepping (`next()`, `previous()`, traversal) leverages the decoded cache for zero-latency bidirectional returns.
   - Invalidation occurs automatically on modification time or size mismatch, manual cache flush (`clearDecodedCache()`), or folder teardown.
2. **Cancellable Cross-Folder Lookahead Prefetching (NAV-07, SYS-02)**:
   - When navigation reaches the boundary zone of a folder (within 3 images of the start or end boundary), `scheduleLookahead()` asynchronously determines the adjacent folder and candidate image via `Navigator::findAdjacentFolderEntry()`.
   - The candidate image is prefetched out-of-process via an isolated `glancelot-decoder` worker (`startPrefetch()`) when memory admission budget permits.
   - Prefetched decoded frames are directly inserted into the decoded cache without affecting the active foreground view or resetting UI state.
   - Crossing the folder boundary subsequently achieves an instantaneous cache hit (`isLastDecodeCacheHit() == true`).
3. **Rigorous Cancellation Lifecycle & Resource Bounds (SYS-02)**:
   - `cancelLookahead()` is invoked synchronously on user navigation changes (`submitNavigation()`, `openFolder()`, `navigate()`), direction reversals, filter updates (`setFilter()`, `clearFilter()`), and sort order mutations.
   - `lookaheadStop` flag and worker termination prevent queued background decoder tasks from starving user-initiated foreground decodes.
   - Memory admission check strictly enforces the system memory allowance before prefetch admission.
4. **Comprehensive Test Suite (`tests/cache_tests.cpp`)**:
   - 10 targeted test cases covering defaults, cache hits on navigation revisit, bidirectional traversal, revision invalidation on disk edits, manual cache clearance, cross-folder lookahead prefetching, and cancellation on reversal or filter changes.

---

## 2. Requirement and Acceptance Criteria Matrix

| Requirement / Acceptance Criteria | Implementation Detail | Evaluation / Evidence | Status |
| :--- | :--- | :--- | :--- |
| **Bidirectional Navigation Hits (PERF-04)** | Stepping next and back hits decoded cache without re-decoding from disk. | Verified in `CacheTests::cacheHitOnRevisit` and `CacheTests::bidirectionalNavigation`. | **Pass** |
| **Revision Invalidation (PERF-04)** | Modifying file size or timestamp on disk invalidates cache entry and triggers fresh decode. | Verified in `CacheTests::revisionInvalidationOnDiskChange`. | **Pass** |
| **Manual Cache Eviction (PERF-04)** | `clearDecodedCache()` evicts entries and resets cache metrics. | Verified in `CacheTests::clearDecodedCache`. | **Pass** |
| **Explicit Open Invariant (PERF-04, Acceptance)** | Explicit user file open re-reads disk even with identical size/mtime; navigation uses cache. | Verified in `AcceptanceTests::explicitOpenDoesNotReuseSameSizeTimestampPixels`. | **Pass** |
| **Cross-Folder Lookahead (NAV-07)** | Prefetches candidate image when within 3 items of folder boundary; crossing achieves cache hit. | Verified in `CacheTests::crossFolderLookaheadPrefetch`. | **Pass** |
| **Lookahead Cancellation (NAV-07, SYS-02)** | Direction reversal or filter modification immediately aborts pending lookahead. | Verified in `CacheTests::lookaheadCancellationOnReversal` and `CacheTests::lookaheadCancellationOnFilterChange`. | **Pass** |
| **Bounded Memory & Isolation (SYS-02)** | Out-of-process prefetch worker adheres to memory budget and isolates decoder faults. | Verified in memory check and process worker lifecycle in `viewer_controller.cpp`. | **Pass** |
| **Zero Telemetry / Offline (SYS-01)** | 100% local operation with zero telemetry. | Codebase inspection confirms zero external network calls. | **Pass** |

---

## 3. Test Evidence

### Windows Test Run (`scripts/build.ps1 -BuildDir build-cache -Test`)

All 23 automated tests passed cleanly:

```
Test project C:/src/Github/Glancelot/.worktrees/cache/build-cache
      Start  1: perf_trace
 1/23 Test  #1: perf_trace .......................   Passed    0.10 sec
      Start  2: navigation
 2/23 Test  #2: navigation .......................   Passed    4.43 sec
      Start  3: directory_listing
 3/23 Test  #3: directory_listing ................   Passed   28.19 sec
      Start  4: file_operations
 4/23 Test  #4: file_operations ..................   Passed    0.33 sec
      Start  5: viewer_acceptance
 5/23 Test  #5: viewer_acceptance ................   Passed    8.22 sec
      Start  6: thumbnails
 6/23 Test  #6: thumbnails .......................   Passed    7.15 sec
      Start  7: folder_tree
 7/23 Test  #7: folder_tree ......................   Passed    6.58 sec
      Start  8: pane_layout
 8/23 Test  #8: pane_layout ......................   Passed    0.39 sec
      Start  9: window_slots
 9/23 Test  #9: window_slots .....................   Passed   20.42 sec
      Start 10: folder_grid
10/23 Test #10: folder_grid ......................   Passed  102.14 sec
      Start 11: file_operation_service
11/23 Test #11: file_operation_service ...........   Passed    1.23 sec
      Start 12: directory_model
12/23 Test #12: directory_model ..................   Passed   19.64 sec
      Start 13: viewport
13/23 Test #13: viewport .........................   Passed    0.12 sec
      Start 14: themes
14/23 Test #14: themes ...........................   Passed    0.24 sec
      Start 15: platform_integration
15/23 Test #15: platform_integration .............   Passed    1.10 sec
      Start 16: grid_search
16/23 Test #16: grid_search ......................   Passed    0.33 sec
      Start 17: grid_density
17/23 Test #17: grid_density .....................   Passed    0.24 sec
      Start 18: ui_settings
18/23 Test #18: ui_settings ......................   Passed    0.33 sec
      Start 19: favorites
19/23 Test #19: favorites ........................   Passed    0.44 sec
      Start 20: folder_history
20/23 Test #20: folder_history ...................   Passed    0.18 sec
      Start 21: shortcuts_help
21/23 Test #21: shortcuts_help ...................   Passed    0.11 sec
      Start 22: filter
22/23 Test #22: filter ...........................   Passed    1.77 sec
      Start 23: cache
23/23 Test #23: cache ............................   Passed    2.25 sec

100% tests passed out of 23

Total Test time (real) = 206.74 sec
```

### Integrated Test Run on main (commit 8b28060)

- **Windows Native CTest (scripts/build.ps1 -Test)**:
  - 23/23 tests passed cleanly (263.94 sec real time).
- **Linux Docker Container (docker build -t glancelot-browser:main .)**:
  - 23/23 tests passed cleanly (135.07 sec real time).

---

## 4. Architectural & Safety Review

1. **Decoder Process Concurrency**:
   - The prefetch worker uses the same isolated `glancelot-decoder` executable architecture as the main viewer, preventing malformed image files from crashing the host process.
   - Worker termination is clean: `cancelLookahead()` safely stops active lookahead execution and cleans up shared memory allocations.
2. **Cache Key Canonicalization**:
   - Cross-platform file path separator inconsistencies are resolved by consistently querying `QFileInfo(path).absoluteFilePath()`.
3. **Threading Model**:
   - `navigationPool` handles directory operations while prefetch decoding runs via dedicated out-of-process decoder invocation, completely avoiding UI thread blocking.

---

## 5. Conclusion & Recommendation

The implementation satisfies all architectural, performance, and behavioral criteria for `CACHE-01`. All 23 tests pass cleanly. **RECOMMENDATION: APPROVE for immediate integration into main.**

