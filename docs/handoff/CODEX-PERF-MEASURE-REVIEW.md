# PERF-MEASURE-01 cross-lane review request

Candidate: codex/perf-measure, e68ebd7 (later evidence-only commits may follow).
Base contract:76da49e; merged main d8aba16 in97e084c.
Reviewer: Claude Code lane, per AGENT-LANES-PLAN.md. This is a request, not approval.
The queue remains owned by the lead. No main merge or integration lock was taken.

## Review surface

- Frozen include/glancelot/perf_trace.hpp is unchanged. Review recorder bounds,
  concurrency, overhead, numeric saturation, percentile math and report privacy.
- Review generation/flag attribution at directory, thumbnail, parent decode IPC,
  file-operation and renderer boundaries. The public Stage enum is unchanged.
- Confirm decode=process-start through validated copied pixels is an acceptable
  existing-stage boundary; it includes worker probe/admission and IPC. Finer codec
  or worker phase timing needs a lead-frozen contract if required. CPU renderer spans
  and Qt frame-swap are explicitly not GPU-completion/physical-presentation metrics.
- Review the opt-in private replay driver, its deadlines and lifetime guard. Normal
  launches never construct it. No policy, public headers or product QML were changed.
- Review fixture generator containment/determinism, independent hash verification,
  Python-O failure guards, raw-distribution aggregation and comparison rejection.
- Review existing deletion-result test correction in4f2fc35. It reveals virtualized
  rows before reading reasons; all expected messages and deletion assertions remain.
- Container display is for isolated replay only: no ports, network disabled, private
  authenticated localhost display, read-only fixtures, own-process cleanup.

## Evidence and open gates

See docs/verification/PERF-MEASURE-01.md for actual commands and outputs, recorder
cost, full111136-file matrix hashes, smoke replay and historical50 native captures.
The50-capture run is816a180, before publication-boundary/main changes, not final
candidate qualification. Power, thermal state, filesystem caches and active GPU
were uncontrolled; a Docker build overlapped. No HDD/SMB/monitor qualification.

Latest full Linux check14/15: folder_grid now passes; platform_integration valid-path
launch assertions fail because qt build stage has no desktop launchers. Requires
lead/Gemini owner resolution. Do not omit the test to claim a green gate. A local,
ignored replay-only Dockerfile produces the runtime separately from that failed gate.
Latest full Windows check14/15: platform passes, grid180s timeout under concurrent
lane builds. Targeted diagnostic in progress; no timeout was raised.

Remaining: final rebuilt Windows check, Linux software-GL replay and repeated capture,
stage-boundary review, full gates, independent approval, serial integration, then
checks on the integrated commit. PLATFORM-01 proposal is separate and cannot become
implementation until the lead freezes its packet and queue entry.
