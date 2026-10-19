# Local performance measurement tools

This is PERF-MEASURE-01 tooling. The packet remains implementing until replay,
worker-phase evidence, cross-lane review and integrated checks are complete.
Nothing uploads data or changes the viewer cache/scheduler policy.

## Build and generate fixtures

Windows, from this lane worktree:

```powershell
./tools/perf/build_tools.ps1
$env:PATH = "C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64/bin;" + $env:PATH
./build-codex-perf-measure/perf-tools/glancelot-perf-fixtures.exe tools/perf/fixtures-v1.json .cache/new-fixtures --smoke
python tools/perf/verify_fixtures.py .cache/new-fixtures
```

Linux, with the pinned Qt 6.8.3 toolchain on PATH/CMAKE_PREFIX_PATH:

```sh
sh tools/perf/build_tools.sh
build-codex-perf-measure/perf-tools/glancelot-perf-fixtures tools/perf/fixtures-v1.json .cache/new-fixtures --smoke
python3 tools/perf/verify_fixtures.py .cache/new-fixtures
```

The Windows wrapper reuses scripts/build.ps1 to import MSVC and the shared .tools
Qt environment; it compiles the viewer but does not rerun unrelated UI tests.
The standalone tools CMake project adds no generator/encoder dependency to the viewer.

Omit --smoke for the full matrix: JPEG and PNG at 12/24/45/60 MP, mixed folders
with exactly 1,000/10,000/100,000 entries, and a six-level binary nested tree.
The generator writes only into a new or empty directory, leaves a generated-root
marker, and refuses to overwrite an existing output. It never removes files.
Use a distinct ignored directory for another run, including after an interrupted run.

Smoke mode explicitly identifies itself with an id ending in -smoke and profile
smoke. Its images are 64x48, mixed directories are capped at 100 entries each, and
its tree depth is two. It is correctness evidence only, never a large-image benchmark.

fixture-index.json records the source manifest hash, Qt version, actual relative
paths, encoded SHA256 hashes and actual image dimensions. The verifier independently
parses JPEG/PNG headers, hashes every file and counts mixed-folder entries.
For comparable reports, use the fixture id plus the index SHA256 as the fixture
identifier. Do not assume encoded bytes match across different Qt/codec builds;
verify the hashes. No user images are required.

## Reports and checks

The viewer's --perf-report option writes schema-1 JSON atomically on quit. It opts
into tracing; GLANCELOT_PERF_TRACE=1 can also enable instrumentation. Set
GLANCELOT_PERF_FIXTURE_ID to the public generated-fixture identifier and
GLANCELOT_PERF_BUILD to the measured version/commit. Unspecified/unversioned metadata
is explicit and cannot pass comparison. No path or EXIF field is accepted in the
performance report context. The older --report option is separate and contains paths.

report.schema.json defines the structural schema. validate_report.py additionally
checks percentile order, split totals and raw/aggregate count agreement. All durations
are nanoseconds on a process-relative steady clock. A generation is stage-local:
worker/navigation request generation or rendered QImage revision. Worker clock origins
must not be subtracted from parent timestamps.

CPU upload/render submission spans do not measure GPU completion, compositor or
physical display latency. Startup ends at a Qt image frame-swap notification, not
proof of calibrated/physically correct presentation. A hidden/unexposed window may
successfully decode without any upload or frame-swap spans. Cold currently denotes
process-first full decode/startup, not a cold filesystem/server/shader cache.

```powershell
./build-codex-perf-measure/perf_trace_tests.exe
python tools/perf/test_report.py build-codex-perf-measure/perf_trace_tests.exe
python tools/perf/test_compare.py build-codex-perf-measure/perf_trace_tests.exe
python tools/perf/validate_report.py .cache/report.json
python tools/perf/compare.py .cache/baseline.json .cache/changed.json --percent 10 --min-samples 50 --split useful
```

Comparison requires matching fixture, platform/backend, measured conditions, equal
sample counts and zero dropped spans. It compares each measured stage's p95 with
an optional JSON --thresholds object, for example {"decode": 5, "upload": 10}.
--noise-floor-ns supplies an explicit absolute tolerance. --split selects a flag
condition such as hit, miss, cold, warm or useful. Useful excludes failed, cancelled
and stale spans. The other conditions can overlap; never sum stage p95 values and
call that end-to-end p95.

Exit codes are 0 (passes thresholds), 1 (regression) and 2 (incomparable/inadequate
evidence). The default 50-sample minimum deliberately refuses a one-open smoke
report as broad benchmark evidence. No-sample stages are reported explicitly.
Thresholds never turn a missing, truncated or mismatched workload into a pass.

## Replay and repeated captures

```powershell
./tools/perf/replay.ps1 -Exe ./build-codex-perf-measure/glancelot.exe -Fixtures .cache/new-fixtures -Output .cache/new-replay -Python python -Runs 1 -Steps 5 -Cycles 2 -RequireRender
```

```sh
sh tools/perf/replay.sh --exe ./build-codex-perf-measure/glancelot --fixtures .cache/new-fixtures --output .cache/new-replay --runs 1 --steps 5 --cycles 2 --require-render
```

Use a new or empty output directory outside the source fixtures. Both wrappers use
replay.py, which verifies all source hashes before and after. The app's bounded,
explicit standard-v1 test driver exercises open, next N, reverse N, folder jumps,
scrolling, combined cursor preview, a real thumbnail cache miss/hit pair, rapid-open
cancellation, corrupt-file failure and recovery. It requires --perf-report and
--no-resume; normal launches do not construct it. No QML/keyboard bindings, decoder
policy or original media are changed. An independent correctness verdict and the
trace assertions must both pass.

Each capture fixes Slate & Ice dark and 1180x780, isolates preferences, records actual
executable SHA256 and source commit, and incorporates the dataset index hash and
steps/cycles into the fixture identifier. For a packaged/container runtime without
Git, replay.py accepts --source-commit with 40 hex digits, explicitly labeling it as
declared provenance while still measuring the binary hash. Logs stay local.

--runs supports 1..50 fresh processes. aggregate.json recomputes percentiles from
raw duration samples, preserves condition splits and omits a fictitious combined
process timeline. aggregate.aggregation.json records input report hashes and capture
count. The tool caps aggregation at 200 captures, 64 MiB per input and two million
spans. It never averages p95 values. A one-run smoke remains inadequate for broad
performance qualification. Startup, filesystem-cache state and native physical
presentation remain distinct concepts.

## Remaining packet work

Linux replay, repeated native/software-rendered baseline capture and comparison,
full viewer Docker checks, owner-controlled baseline failures, worker phase boundary
review, independent Claude review and integration remain open. Hardware/SMB/high-refresh
claims require separate measured evidence.
