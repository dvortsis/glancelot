# PERF-MEASURE preimplementation audit

Codex branch: `codex/perf-measure`; worktree: `.worktrees/codex-perf-measure`.
Base inspected: `9f651a2`. Source: the user-approved remaining-items lane plan.

## Contract gate

The lead is preparing `docs/PERF-MEASURE-PLAN.md`, `include/glancelot/perf_trace.hpp`
and `docs/handoff/CODEX-WAVE1.md` in the Claude lanes worktree. They were not yet
committed on main at this audit. Do not copy uncommitted shared contracts into
this branch or change another lane's checkout. Rebase/merge the committed main
contract before implementing. The existing platform-integration edits on main
belong to the concurrent platform-operations task and are excluded.

## Observed integration points

- `ViewerController::Private::decode` deliberately bypasses its retained decoded
  cache. Record that as a miss/bypass; never fabricate a hit from a fast decode.
- `ThumbnailService::request` has a genuine memory-cache hit path, queued for
  delivery. Exercise that path to prove CacheHit reporting without changing cache
  policy owned by Gemini.
- The existing `first_image_frame_ms` checks that the viewer has a non-null image
  when a window frame swaps. It does not correlate that frame with the uploaded
  revision or prove physical presentation. Keep render submission, frame-swap,
  and physical-display evidence distinct.
- Full decode IPC is bounded JSON plus shared-memory pixels. Instrument request
  admission, process start/response and parent transfer separately. Worker clock
  origins differ: report worker durations, never subtract timestamps from two
  processes as if they shared an origin.
- `DirectoryEntryModel` constructs rows and its key index on the worker, then
  publishes them on the GUI thread. Attribute enumerate/sort/publish separately,
  and mark cancelled/stale generations instead of counting them as useful work.
- `ImageCanvas` queues QRhi texture uploads. CPU time issuing upload commands is
  not GPU completion or monitor latency. Labels and report schema must say so.
- The reporting context must reject/redact path-like caller strings as well as
  avoid paths in spans. Existing `--report` includes image_path and must not be
  copied wholesale into a redacted performance report.

## Questions for the contract lead

1. Confirm the full-image bypass plus real thumbnail hit is the intended hit/miss
   acceptance evidence; enabling full-image cache hits is explicitly out of scope.
2. The replay matrix includes next/reverse/jump/scroll/combined actions, while the
   only new app option pre-approved is --perf-report. Prefer a test-only replay
   driver (or an explicitly approved app replay hook) over platform pointer
   automation and extra GUI dependencies. Freeze the permitted route before
   implementing it; no edits to Main.qml are planned.
3. Decoder and canvas test targets also need to link the core recorder so trace
   definitions resolve. Confirm those CMake linkage additions with the lead.

## Baseline evidence in progress

- Windows: scripts/build.ps1 -BuildDir build-codex-perf-measure -Test.
  Local log: .cache/perf-measure-baseline-windows.log.
- Linux: wsl -d Ubuntu -- docker build -t
  glancelot-browser:codex-perf-measure-baseline <this worktree>.
  Local log: .cache/perf-measure-baseline-linux.log.

Neither run is recorded as passed until its terminal process and complete test
output are inspected. Hardware/SMB/high-refresh gates remain unverified.

## Remaining scope

This audit starts PERF-MEASURE only. The active goal still covers all Codex rows
in the lane plan, including PLATFORM-01 and later waves, their dependencies,
cross-lane reviews, serial integration and physical qualification boundaries.
