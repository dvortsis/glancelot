# PERF-MEASURE-01 contract and delivery

The lead froze this contract on October 18, 2026.

| | |
| --- | --- |
| Lane | Codex |
| Requirements | SYS-05, PERF-04, PERF-05 |
| Source | PERFORMANCE-STRATEGY.md, packet PERF-MEASURE (§3, §4 and §16) |
| Branch | `codex/perf-measure` |
| Worktree | `.worktrees/codex-perf-measure` |
| Build directory | `build-codex-perf-measure` |
| Reviewer | Claude Code lane |

## Frozen interface

`include/glancelot/perf_trace.hpp` declares the `Stage` and `Flag` enums, `Span`, `ReportContext`, `Recorder`, `ScopedSpan` and `toJsonReport`. Implement it exactly. Any change to the header goes through the lead.

These additions are pre-approved:

- `src/core/perf_trace.cpp` added to `glancelot_core`, with no Qt dependency;
- a `perf_trace` core test target (`tests/perf_trace_tests.cpp`);
- a `--perf-report <file>` option in `app/main.cpp`, which writes the JSON report on quit;
- `tools/perf/` scripts: the fixture generator, baseline replay and comparison;
- `docs/verification/PERF-MEASURE-01.md`.

## Behaviour

- **Instrumentation points.** Add `ScopedSpan` at each existing stage boundary, without changing behaviour:
  - directory listing (enumerate and sort) in `src/core/directory_listing.cpp` and `navigation.cpp`;
  - model publication in `directory_entry_model.cpp`;
  - the thumbnail queue and decode in `thumbnail_service.cpp`;
  - the decode queue and decode in `viewer_controller.cpp` and the worker IPC;
  - texture upload and present in `src/render/image_canvas.cpp`;
  - the cache lookup with the CacheHit flag;
  - mutations in the file-operation service;
  - startup, up to the first presented frame.
- **Generations.** Spans carry the request generation, so a stale or cancelled result is flagged (`Stale`, `Cancelled`) rather than counted as useful latency.
- **Fixtures.** Generate deterministic, disposable fixtures from a versioned manifest:
  - JPEG and PNG at 12, 24, 45 and 60 MP;
  - mixed folders with 1k, 10k and 100k entries;
  - nested trees.
  The manifest ID goes in the report, and user paths never do.
- **Replay.** `tools/perf/replay.ps1` (and a `.sh` for Linux/Docker) runs scripted sessions: open, next ×N, reverse, folder jump, grid scroll and combined preview. It writes the JSON report, and `compare` diffs two reports with per-stage thresholds.
- **Overhead.** Measure the cost with the recorder enabled and disabled. Report it, and fail the test if the disabled per-span cost exceeds a stated bound.
- **Boundaries.** This packet adds measurement only. It changes no cache, scheduler or decode policy (CACHE-01 and IO-01 are Gemini-lane tasks). No network access and no telemetry.

## Verification

- **Core tests:**
  - the ring bound and drop count under multithreaded load;
  - drain ordering;
  - disabled-path cost;
  - JSON schema validity;
  - the percentile math on known inputs;
  - no paths in the output.
- **App:** `--perf-report` on a generated fixture produces the report, both with and without cache hits. It demonstrates the hit/miss distinction and the cold/warm split.
- **Full suite and Linux:** `scripts/build.ps1 -BuildDir build-codex-perf-measure -Test`, plus `docker build`. Record Windows and Docker baselines, including platform, backend and the actual numbers, in the verification document.
- **Unverified unless measured:** physical HDD, SMB and high-refresh hardware.
