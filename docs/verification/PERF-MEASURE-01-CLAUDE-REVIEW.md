# PERF-MEASURE-01 independent Claude Code lane review

**Disposition: APPROVED (with one Medium non-blocking condition, finding 1).**
Reviewed candidate: `8e2b1fd` (branch `codex/perf-measure`, reviewed in `claude/review-perf-measure`).
Reviewer: Claude Code lane (did not implement). Date: 2026-10-19.
This is a review, not an integration. The root main queue and its `integration_lock` stay authoritative.
Nothing was merged, pushed or published. Product source, frozen headers and the queue are untouched.

## Scope read

AGENTS.md, AGENT-LANES-PLAN, PERF-MEASURE-PLAN (plus the lead clarification in `91a49d2`),
the Codex review handoff, the verification doc, the full diff `5d889c9..8e2b1fd`
(recorder, instrumentation, `--perf-report`, replay driver, `tools/perf/*`, both test prerequisites).
`include/glancelot/perf_trace.hpp` is unchanged versus the merge base. `git diff 5d889c9 HEAD -- include` is empty
for it, and the file matches the frozen contract.
No QML changed (`ui/` absent from the diff), so no UI-skill review was needed. No theme/branding is touched.
AgentMemory MCP was not queried in this session. I did not invent memory content.

## Independent results (actual output)

| Check | Result |
| --- | --- |
| `scripts/build.ps1 -BuildDir build-claude-review-perf-measure -Test` | 100% tests passed, **15/15**, Total Test time 212.09 s (`folder_grid` 104.54 s, `platform_integration` 3.29 s) |
| `perf_trace_tests.exe` | `PASS ring_bound multithread_drops drain_order flags percentiles redaction monotonic_clock`; `disabled_ns_per_span=2.4468 enabled_ns_per_span=76.928 disabled_bound_ns=1000` |
| `test_report.py`, `test_aggregate.py`, `test_compare.py <my perf_trace_tests.exe>` | all PASS, also under `python -O` for report |
| `test_validation_guards.py` | PASS normal and `-O` |
| Smoke fixtures generated with my built `glancelot-perf-fixtures.exe`, then `verify_fixtures.py` | `PASS ... files 316 profile smoke` |
| `test_fixture_integrity.py` | PASS normal and `-O` (complete-manifest, dimension and extra-file rejection) |
| `replay.py --runs 3 --steps 3 --cycles 2` (native, no `--require-render`) | 3/3 PASS, next/previous 6, cache, grid, combined, cancellation, corrupt decode and recovery; fixture re-verified, source hashes unchanged |
| Tampered report (`decode.all.p95_ns += 5`) fed to `compare.py` | exit 2 `incomparable`; `validate_report.py` says `raw/aggregate metrics mismatch` |
| `docker build -t glancelot-browser:claude-review-perf-measure .` | built; in-image CTest **15/15**, Total Test time 113.19 s; image tagged |

Replay report from my run: 0 dropped, enumerate 42, sort 76, thumbnail_queue 83 (20 cancelled),
thumbnail_decode 63 (11 cancelled, 2 failed), decode 20 (1 cold, 1 cancelled, 1 failed, 1 stale), cache_lookup 104 (1 hit).
Cancelled, failed and stale attribution is exercised end to end.

## Findings

1. **Medium (non-blocking, needs a doc caveat before any hit-rate use)** — `src/app/viewer_controller.cpp:325`.
   The viewer's `CacheLookup` span wraps `cacheKey(file)`. That is a `QFileInfo` stat plus string build (lines 242-245).
   The viewer's image cache is never read (the code comment says "reserved for a future validated-revision prefetch path"),
   so those spans can never be `CacheHit`.
   Every explicit open therefore adds a `cache_lookup` **miss** that is really a filesystem stat.
   Scenario: my run reports `cache_lookup` count 104 with 1 hit, so the miss split is dominated by stat calls, and `cache_lookup` p95 and total include disk-stat time.
   A reader computing a hit rate or "lookup latency" would get a wrong number.
   The replay coverage gate passes only because thumbnail-service hits exist. Real lookups are in `thumbnail_service.cpp:236`.
   Requested fix (lead or Codex, not done by me): either document in the verification doc and `tools/perf/README.md` that viewer `cache_lookup` misses are key-building, not lookups,
   or move the span so it covers a real lookup. No policy change is implied.
2. **Low** — `tests/perf_trace_tests.cpp:36-38`. The overflow test proves size, drop count and sort order, but not that the *oldest* spans are the ones evicted.
   Each worker's start times are descending (`each - i`), so a "drop newest" bug would pass. The disabled-cost bound (`:17`, 1000 ns) is very loose against the measured 2.4 ns.
   It is a stated bound as required, but it would not catch a 100x regression.
3. **Low** — `tools/perf/container_replay.sh:18-26`. The `mktemp -d` work directory, which holds the VNC password file (mode 0600), is never removed by the `finish` trap.
   It only lives in the unpublished container, localhost-bound, so the exposure is small. It does leak `/tmp/glancelot-perf-display.*` per run.
4. **Low** — `tools/perf/generate_fixtures.cpp:62` allows tree `branching <= 3` while `tools/perf/verify_fixtures.py:84` allows `<= 4`. A manifest with branching 4 is accepted by the verifier but cannot be generated. Harmless today (manifest v1 is fixed).
5. **Info** — `src/render/image_canvas.cpp:71` records a `Present` span on every frame. At 60 fps the 65,536-span ring covers about 18 minutes before evicting early startup/decode spans.
   Drops are counted and the replay and aggregation reject nonzero `dropped`, so this is safe, but long manual `--perf-report` sessions can be truncated. `Present`/`Upload` generation is `QImage::cacheKey`, not a request generation.
6. **Info** — `Recorder::record` is `noexcept` but takes a `std::mutex`. A `system_error` there would terminate. Practically unreachable.
   A report without `spans` is only checked for internal consistency, so a hand-fabricated aggregate-only report can pass `compare`.
   That is acceptable for local-trust tooling, and `aggregate` refuses span-less input.

## Audit notes (no defect found)

- **Recorder.** Single static array of 65,536 spans, no per-span allocation. A mutex guards the ring, `enabled` is a relaxed atomic, and disabled cost is one load (2.4 ns measured).
  Drop-oldest with a `dropped` count, invalid (negative or reversed) spans are rejected, and totals saturate.
  Nearest-rank percentile `ceil(n*p/100)` matches the independent Python (`(n+1)//2` for p50) on 100 known samples.
  The JSON context is whitelist-validated (paths, URLs and newlines become `redacted`) and the report holds no paths. Test and Python validation both cover this.
- **Generation attribution.** Stage-local tokens: directory-model generation, navigation `sequence` through `GenerationContext`, thumbnail epoch, decode `generation`.
  Stale, cancelled and failed flags are set at the correct sites. `useful` excludes all three.
  `shutdown()` cancels pending and busy thumbnail spans exactly once (`retire` does not re-record).
  Decode queue ends on `QProcess::started`, and decode runs to validated pixels. Both match the lead clarification as end-to-end parent IPC timings, not codec-only.
  Upload and Present are CPU submission and startup is the first Qt frame swap; the doc states these are not GPU or photon latency.
- **`--perf-report`.** Writes via `QSaveFile` on `aboutToQuit`, a failed save returns exit code 4, and normal launches never construct the replay.
  `installReplay` needs `--perf-report`, `--no-resume`, the explicit `GLANCELOT_PERF_REPLAY=standard-v1` and bounded steps and cycles.
  It issues only existing read-only viewer calls (next, previous, folder jump, view modes, open), with a 300 s overall and 30 s per-step deadline. No mutation or shell dispatch exists, and the replay asserts `mutation` count 0.
- **Tooling.** `verify_fixtures.py` independently checks the full expected file set, hashes, dimensions, extra files and directories, symlinks and containment, with a bounded file count.
  `aggregate.py` merges raw durations only, never percentiles or cross-process timestamps, and rejects dropped or span-less captures.
  `compare.py` rejects unversioned builds, unspecified or mismatched conditions, dropped spans, mismatched sample counts and bad thresholds.
- **Prerequisite `4f2fc35`.** Waits for the model count (2), uses `positionViewAtIndex` and `itemAtIndex` per row, and keeps all original assertions (2 reasons, "open in another app", "Nothing was deleted").
- **Prerequisite `14e7c46`.** Disposable symlinked launchers capture real argv and cwd. `PATH` is restricted to the temp launcher directory, so the real `xdg-open` and terminals can never run.
  The tests still assert literal and special-character path routing, file-to-parent cwd, `TERMINAL` override, fallback when the override is missing, and `false` with no launchers and no capture file.
  Invalid-input tests assert nonlaunch. Environment is restored in `cleanupTestCase`. The Windows path is unchanged (`#ifndef Q_OS_WIN`). No production code is touched.

## Limitations

- **Native render stages not reproduced here.** In this reviewer session the window produced zero frame swaps, even for the pre-existing `--report` path (`"frames": 0`, `"first_image_frame_ms": -1`, `graphics_api` 4/D3D11).
  The `--require-render` replay therefore failed with "native render coverage missing" (upload, present and startup counts 0). This is an environment limit of this session (likely no presenting desktop), not attributed to the candidate.
  The author's `-RequireRender` evidence and the Docker software-GL smoke replay in the verification doc were **not independently reproduced by me**.
  I ran the native replay without `--require-render`, and the Docker build with its in-image tests, but not the runtime-image smoke replay or the 50-capture and 111,136-file full-dataset runs.
- Physical HDD, SMB, ICC, DPI, HDR, high-refresh, active GPU identity, large-MP decode and 100k-folder interactive latency remain unqualified. Nothing here is hardware qualification.
- I did not use any private or user media. All fixtures were generated and disposable.

## Remaining integrated checks (lead)

Merge current `main` (Gemini clipboard changes) into the branch and re-run `scripts/build.ps1 -Test` and `docker build -t glancelot-browser:main .` under the `integration_lock`.
Confirm the finding 1 documentation caveat landed, re-run a `-RequireRender` replay on a presenting desktop, and record evidence before marking done.
