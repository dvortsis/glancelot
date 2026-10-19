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
It does not pretend to isolate codec CPU time. These are the existing parent IPC boundaries. Claude/lead review must accept this
interpretation or freeze a separate worker-phase contract; this lane does not expand
the frozen Stage enum. Publication now includes GUI-thread result retrieval through
model application, ending before the next request is pumped.

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
launching; the test expects rejection. Do not repair it in this lane. This historical failure predates the main merge described below; the updated
Windows platform test now passes. Final candidate checks are recorded separately.

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

WSL itself has no CMake installed. Linux core verification ran in the existing
`glancelot-qt:dev` Docker toolchain with this checkout mounted read-only:

```
100% tests passed, 0 tests failed out of 4
Total Test time (real) = 2.86 sec
disabled_ns_per_span=1.97817 enabled_ns_per_span=83.1071 disabled_bound_ns=1000
```

That older image lacks Qt's ICU/GLib link dependencies and cannot link the fixture
tool. The current Dockerfile `qt` stage is used instead for Qt tool verification;
no host libraries or viewer policy were changed to work around it.

## Fixture and comparison tooling

The versioned manifest, Qt-only offline generator, Windows/Linux build wrappers,
independent dimension/hash/count verifier and result schema are under tools/perf.
Windows smoke generation completed twice with byte-identical indexes (316 files
each). Every hash was independently checked; JPEG/PNG dimensions and actual folder
entry counts were verified. Reusing an existing output and targeting a nonempty
sentinel directory both returned failure while leaving existing data unchanged.
Linux generation and verification also passed in the current pinned Qt Docker stage:

```
Generated glancelot-perf-fixtures-v1-smoke: 316 files; Qt 6.8.3; profile smoke
PASS fixture glancelot-perf-fixtures-v1-smoke files 316 profile smoke
index_sha256 c87fae45141806538c3ac9c12dbe422356b4c2c8d07c5ec9b312ed0b1529cbea
```

The index hash matches both Windows smoke runs. The Linux wrapper initially failed
because its working-tree file had CRLF despite the Git blob's LF normalization;
actual LF bytes and the local .gitattributes rule resolve it. The successful rerun
used the shipped build_tools.sh and independent verify_fixtures.py.
The full matrix also completed and passed independent verification:

```
Generated glancelot-perf-fixtures-v1: 111136 files; Qt 6.8.3; profile full
PASS fixture glancelot-perf-fixtures-v1 files 111136 profile full
index_sha256 9c5b6da1ffa6d467d5b9f496bd4bbd5d426fe544153353794bd04c82abd9e1e7
```

This checks every generated hash, JPEG/PNG dimensions (including all four actual
MP sizes), and exact 1k/10k/100k entry counts. It is not a claim that every large
image is admitted by the existing viewer memory policy.

The comparison checker passes identical-data, real 100-percent p95 regression,
per-stage threshold, noise-floor, inadequate-sample, mismatched-fixture, dropped-trace
and invalid-option cases. It refuses missing conditions, unversioned builds,
truncated traces and unequal sample counts. The documented default minimum is 50
samples; this does not turn the one-image native smoke into performance qualification.

## Replay implementation and first native workflow check

The private driver is inactive unless GLANCELOT_PERF_REPLAY=standard-v1, --perf-report
and --no-resume are explicitly supplied. It has bounded steps/cycles, per-action and
overall deadlines. It invokes existing read-only viewer/grid APIs and writes a separate
path-free correctness verdict. It dispatches no mutation or shell commands and makes no
QML or public-header changes. Normal launches do not construct a replay driver.

replay.ps1 and replay.sh call the shared replay.py runner. It verifies source fixtures
before and after, uses isolated preferences and new/empty output directories, records
executable SHA/build identity and fixed window/theme/workload settings, checks action
counts and trace flags, and refuses incomplete/failed/truncated results. Fixture
verification now uses explicit checks, so Python -O cannot bypass integrity validation.

The first native Windows run (steps=3, cycles=2, D3D11 requested, RequireRender) passed:

```
PASS standard-v1 run 0 next/previous 6 cache hit/miss, grid, combined,
  cancellation, corrupt decode and recovery
```

Observed action counts: next=6, previous=6, next/previous folder=1 each,
grid_scroll=6, combined selection=1, thumbnail probes=2, burst opens=2,
corrupt open=1 and recovery open=1. The real thumbnail cache produced one hit;
trace flags included cancelled=20, stale=1 and failed=3 across stages. Upload,
render-submission, publication and startup spans were all present. Every original
generated-fixture hash remained unchanged. This is workflow/trace evidence, not
a statistically qualified latency result.

The aggregate.py tool recomputes nearest-rank percentiles from original raw duration
samples. It refuses incompatible build/conditions, dropped traces, missing raw samples
and bounded-input overflow. It never averages percentiles or merges timestamp origins.
Known-distribution tests pass for exact p50/p95/totals and incompatible/truncated inputs.
Repeated native capture and a comparison pipeline check are recorded below.
Linux software-rendered replay remains in progress.

## Still required before review

- Full fixture verification passed; retain its evidence when exercising replay on the larger mixed folders.
- Exercise the complete replay on Linux/Docker and collect repeated comparable native/software-rendered baselines; first native workflow passed.
- Use comparison on sufficiently repeated, equivalent replay workloads; tooling tests alone do not qualify performance.
- Claude review of the documented existing IPC/render stage boundaries. Finer
  worker-local phase timing has no frozen interface in this packet.
- Full Linux viewer/Docker gate and final Windows checks; resolve owner-controlled
  platform-test environment failures. Do not count the replay-only artifact as a passing gate.
- Claude-lane independent review, serial integration and integrated-commit Windows/Linux checks.

Physical HDD, SMB, monitor DPI, ICC/HDR and high-refresh behavior are **unverified**.
This packet and the remaining Codex lane inventory are not marked done.

## Latest candidate checks and repeated native baseline

Main d8aba16 was merged cleanly in 97e084c. Gemini's missing-path platform fix and
QML changes remain theirs; Codex did not edit their implementation. Commit 4f2fc35
corrects an existing test's virtualized-results inspection: wait for model count 2,
reveal each row, obtain its delegate, then assert both original error messages.
All deletion, partial-failure and Stop/cancellation assertions remain in place.
It introduces no product behavior or QML change.

Latest full Linux command (candidate 4f2fc35):

```
docker build -t glancelot-browser:codex-perf-measure .
folder_grid Passed 120.50 sec
platform_integration Failed: four valid-path launcher assertions returned false
93% tests passed, 1 tests failed out of 15
Total Test time (real) = 169.63 sec
```

The Qt build stage has no xdg-open or supported terminal executable. These tests
invoke real desktop launchers and assert success. A headless build-stage test needs
an owner-approved launch test seam or explicit desktop test environment. Do not
silently skip it, replace launchers with success stubs, or claim the full gate passed.
A separately labelled local replay-only Dockerfile omits the failed test command
solely to produce a runtime for instrumentation replay; it is not the required
Docker gate and is not a repository Dockerfile change.

Latest full Windows command (candidate 4f2fc35):

```
./scripts/build.ps1 -BuildDir build-codex-perf-measure -Test
platform_integration Passed 6.64 sec
folder_grid Timeout 180.21 sec
93% tests passed, 1 tests failed out of 15
Total Test time (real) = 352.22 sec
```

This ran concurrently with Docker compilation and the separate cross-lane review
build. The timeout is unresolved; a verbose targeted run is in progress. No timeout
limit or assertion was weakened. Final checks must rebuild the publication-scope
adjustment before review.

The historical native baseline uses commit 816a180's executable, before the main
merge and publication-scope adjustment. It must not be silently reused as the final
candidate's baseline. All 50 fresh processes passed standard-v1, steps 3, cycles 2,
RequireRender; aggregate contains 21,285 raw-derived spans. Independent report
validation and min-samples 50 self-comparison passed. Self-comparison verifies the
pipeline only and is not an optimization A/B claim.

Useful p95 observations in milliseconds: enumerate 10.3997; sort 0.0188;
publish 82.2885; thumbnail_queue 120.1973; thumbnail_decode 66.1675;
decode_queue 10.2564; decode 181.237; upload 0.0061; present 0.5309;
cache_lookup 0.1865; startup 1816.7574. These are the tiny smoke fixture workload,
not 24 MP qualification. Mutation is absent because replay never mutates media.

Conditions: Windows 11 Pro 10.0.26200, Intel i7-12700H, Qt 6.8.3, requested D3D11
(report qt-api-4), 1180x780, Slate & Ice dark. Installed Intel Arc A370M and Iris Xe
are candidate inventory only; the active GPU was not verified. Power/thermal state
was uncontrolled, filesystem caches were not flushed, and a Docker build overlapped.
No physical-presentation, R1, HDD or SMB claim follows. Every source fixture hash
was checked before/after. Ignored evidence lives under .cache/perf-native-baseline-50
and .cache/perf-measure-native-baseline-50.log.

Negative-path checks also passed: invalid replay configuration exits 2, an unwritable
report destination exits 4, and Python -O cannot bypass fixture integrity validation.

## Complete fixture coverage guards

Commit d36c587 makes verification derive every expected path and image dimension
from the versioned manifest, including mixed folders and nested trees. It rejects
missing groups/files, dimensions inconsistent with the manifest, mismatched pinned
Qt provenance and unexpected files/directories. It bounds report size and expected
entry counts before allocating the expected dataset. The generated fixtures are
unchanged. Eight corruption cases pass on Windows both normally and with Python -O,
and on Linux with Python -O in the isolated runtime container:

```
python tools/perf/test_fixture_integrity.py .cache/perf-fixtures-smoke-a
python -O tools/perf/test_fixture_integrity.py .cache/perf-fixtures-smoke-a
PASS complete manifest coverage, dimensions, toolchain and unexpected-file/directory rejection
```

This closes an index-subset validation gap; it does not alter the replay workload
or application binary. A strict full-matrix recheck is running separately. The
Linux repeated baseline's application binary is SHA256-prefix 142e27c0c383f56d,
source revision dd0820eec0546082fd4d026ea5f7411be2249b52. Its first exploratory
smoke supplied an incorrect declared commit suffix and is excluded from provenance
and baseline evidence. The repeated run reads the exact source revision from Git.
