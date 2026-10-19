# PERF-MEASURE-01 implementation evidence

Status: **implementing**, not approved, not integrated. Branch `codex/perf-measure`,
worktree `.worktrees/codex-perf-measure`, build `build-codex-perf-measure`.
Contract base: main `76da49e`; public `perf_trace.hpp` is unchanged.
Authoritative full scope: `docs/AGENT-LANES-PLAN.md` and `docs/PERF-MEASURE-PLAN.md`.

## Delivered so far

- Qt-free fixed ring (65,536 spans), oldest-record eviction/drop accounting,
  multithreaded recording, monotonic process-relative nanoseconds and start-ordered drain.
- Disabled-by-default RAII spans, enabled by `GLANCELOT_PERF_TRACE=1` or `--perf-report`.
- Schema-1 JSON with nearest-rank percentiles, bytes/counts and independent flag splits:
  hit/miss, cold/warm, cancelled, failed, stale and useful. Useful excludes failed,
  cancelled and stale samples. Totals saturate on uint64 overflow.
- Report contexts accept bounded identifiers only. Path-like or unbounded strings are
  replaced by `redacted`; paths, EXIF and error messages never enter spans.
- Private thread-local generation propagation keeps frozen public core signatures intact.
- Existing enumerate, sort, model publication, thumbnail queue/decode, cache decision,
  full decode launch/IPC completion, mutation, texture upload, CPU render submission,
  and first image frame-swap boundaries are instrumented without changing policy.
- `--perf-report <file>` writes atomically on quit, reports write failure via exit code 4,
  and includes raw bounded spans. Default reports do not reuse the legacy path-bearing report.

The only CMake edits are the approved core source/test registration plus linking the
existing viewport test to that core recorder, which its newly instrumented renderer uses.
No QML, scheduling, cache policy, frozen public header or Gemini implementation is changed.

## Stage interpretation

`decode_queue` is full-worker process launch wait. `decode` runs from process-start
notification through validated copied pixels, including worker probe/admission and IPC.
It does not pretend to isolate codec CPU time. Worker-local phase timings remain a
follow-up item in this packet.

`upload` and `present` measure CPU command issuance and renderer callback work;
they are not GPU completion or monitor presentation. `startup` ends on Qt's first
frame-swap notification after a non-null image. That is not physical correctness
or input-to-photon evidence. The render revision uses QImage's cache key; navigation
and decode use their request generations. No cross-process clock origins are subtracted.

`Cold` currently means the first full decode in this process (and startup). It does
not mean a cold filesystem, NAS, shader or OS cache. A cache miss remains distinct
from a cold run. Full-image explicit opens deliberately bypass the retained cache;
only real thumbnail memory-cache hits set CacheHit. Replay must exercise both.

## Windows evidence

On Windows 11, MSVC 2022, Qt 6.8.3, Release:

```
./scripts/build.ps1 -BuildDir build-codex-perf-measure
PASS ring_bound multithread_drops drain_order flags percentiles redaction monotonic_clock
disabled_ns_per_span=2.0235 enabled_ns_per_span=69.417 disabled_bound_ns=1000
python tools/perf/test_report.py build-codex-perf-measure/perf_trace_tests.exe
PASS independent JSON parse, exact percentiles, hit/miss/cold/warm splits, stale exclusion and invalid-schema rejection
```

The invariant test uses four concurrent producers (80,000 records) and asserts the
65,536 ring size and exact overflow count. The overhead experiment uses 1,000,000
disabled spans and 100,000 enabled spans. These local measurements establish recorder
cost only, not application or hardware qualification.

The full required Windows run reached:

```
./scripts/build.ps1 -BuildDir build-codex-perf-measure -Test
93% tests passed, 1 tests failed out of 15
Total Test time (real) = 227.60 sec
The following tests FAILED:
  15 - platform_integration (Failed)
```

The baseline before production edits failed the same Windows platform suite (13/14
passed, 221.28 sec). That adapter belongs to PLATFORM-OPS-01/Gemini. Static inspection
finds `openTerminal` maps a nonexistent requested path to its existing parent before
launching; the test expects rejection. Do not repair it in this lane. The latest
trace-attribution-only source edits still need an incremental rebuild and checks.

A generated 1024x768 PNG was opened by a timed foreground native viewer with isolated
preferences, `--perf-report`, `--no-resume`, and D3D11 requested:

```
PASS schema-1 metrics, splits, redacted context and raw/aggregate counts
decoded 1024 768 loading False graphics_api 4 frames 13
nonempty spans: enumerate=1 sort=2 thumbnail_queue=1 thumbnail_decode=1
  decode_queue=1 decode=1 upload=1 present=3 cache_lookup=2 startup=1
```

A prior hidden-window run decoded and wrote valid JSON but produced no frames; it is
not presentation evidence. That run verified the source hash was unchanged. Native
report files are under ignored `.cache/perf-smoke/`; no fixture paths are in the
performance JSON. Fixture generation here was a smoke-only temporary command, not
the required versioned manifest generator.

## Linux baseline and work in flight

The pre-change Docker viewer baseline compiled but failed an existing UI test:

```
93% tests passed, 1 tests failed out of 14
Total Test time (real) = 116.93 sec
GridUiTests::deleteBatchProgressStopAndPartialFailureResults
Actual reasons.size(): 1; expected: 2
```

WSL itself has no CMake installed. Linux core verification is instead running in the
existing `glancelot-qt:dev` Docker toolchain with this checkout mounted read-only.
Results are not claimed until the command completes and its output is inspected.

## Still required before review

- Versioned deterministic JPEG/PNG 12/24/45/60 MP, 1k/10k/100k mixed-folder and nested-tree fixture generator.
- Windows and Linux replay scripts covering open, next, reverse, folder jump, grid scroll and combined preview,
  with correctness verdicts, genuine thumbnail hits/misses, cancellation/stale/failure cases and repeat counts.
- Per-stage baseline comparison with thresholds and validation of comparable conditions.
- Detailed worker IPC phase measurements or lead-reviewed stage-boundary resolution.
- Rebuild latest changes; full Linux viewer/Docker checks; resolve owner-controlled baseline blockers.
- Claude-lane independent review, serial integration and integrated-commit Windows/Linux checks.

Physical HDD, SMB, monitor DPI, ICC/HDR and high-refresh behavior are **unverified**.
This packet and the remaining Codex lane inventory are not marked done.
