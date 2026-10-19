# PERF-MEASURE-01 cross-lane review request

Candidate: codex/perf-measure, implementation through d36c587 plus evidence descendants.
Application binary source is unchanged since e68ebd7; d36c587 tightens fixture tooling.
Base contract: 76da49e; merged main d8aba16 in 97e084c.
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
- Review existing deletion-result test correction in 4f2fc35. It reveals virtualized
  rows before reading reasons; all expected messages and deletion assertions remain.
- Container display is for isolated replay only: no ports, network disabled, private
  authenticated localhost display, read-only fixtures, own-process cleanup.

## Evidence and open gates

See docs/verification/PERF-MEASURE-01.md for actual commands and outputs, recorder
cost, full 111,136-file matrix hashes, smoke replay and historical 50 native captures.
The 50-capture run is 816a180, before publication-boundary/main changes, not final
candidate qualification. Power, thermal state, filesystem caches and active GPU
were uncontrolled; a Docker build overlapped. No HDD/SMB/monitor qualification.

Latest full Linux check 14/15: folder_grid now passes; platform_integration valid-path
launch assertions fail because qt build stage has no desktop launchers. Requires
lead/Gemini owner resolution. Do not omit the test to claim a green gate. A local,
ignored replay-only Dockerfile produces the runtime separately from that failed gate.
Latest full Windows check 14/15: platform passes, grid 180s timeout under concurrent
lane builds. A targeted diagnostic also timed out at 180s during overlapping lane tests.
The final rebuilt full suite is running; no timeout was raised.

Linux 50-capture replay passed (16,527 spans, zero drops, source hashes unchanged).
The rebuilt Windows full suite remains 14/15 due to grid 180s timeout; the changed
deletion/Stop test passes 3/3 in 1927ms. Remaining: grid diagnosis and full gates,
stage-boundary review, full gates, independent approval, serial integration, then
checks on the integrated commit. PLATFORM-01 proposal is separate and cannot become
implementation until the lead freezes its packet and queue entry.

The independent fixture verifier was subsequently tightened to require complete
manifest coverage (including every tree entry), image dimensions and Qt provenance,
and reject unexpected files/directories. Eight corruption cases pass normally and
under Python -O. This affects tooling validation, not the captured application binary.
