# PERF-MEASURE-01 combined cross-lane review request

Current candidate: codex/perf-measure, affabf4 plus evidence-only descendants.
Reviewer: Claude Code lane. This request is not approval or main integration.
The user appointed this session lead; serial candidate synchronization/checks hold
main's integration_lock as codex-lead. Only lead contracts/queue updates are on main.

## Candidate contents

- Bounded recorder, generation/flag attribution, --perf-report, deterministic offline
  fixtures, private opt-in workflow replay, raw aggregation and threshold comparison.
  Frozen include/glancelot/perf_trace.hpp remains unchanged.
- Tooling through 3885ca7 independently validates every raw-derived metric/split,
  complete manifest coverage and unexpected files/directories. All 101 saved captures
  passed stricter validation. Invalid/truncated/incomparable data cannot pass compare.
- Existing virtualized deletion-result test correction 4f2fc35 reveals rows before
  reading their reasons and retains all original deletion/Stop/message assertions.
- Lead-authorized test-only Linux platform-launcher prerequisite 14e7c46, included
  in affabf4 for combined independent review. Disposable child processes capture
  exact argv/cwd; tests assert literal path/file-parent routing, nonlaunch on invalid
  inputs, TERMINAL override/fallback and failure with no launchers. No production
  platform adapter or public header is changed by the repair. Read
  docs/verification/PLATFORM-TEST-ENV-01.md.
- Main synchronization 70b3652 includes lead PLATFORM contract and the existing
  CLIPBOARD-OPS method declarations/stubs. Those stubs are not clipboard feature
  completion and remain Gemini's implementation responsibility.

## Required review focus

1. Recorder concurrency, fixed allocation bound, drop semantics, disabled cost,
   monotonic timing, saturating totals, percentile math and path-free JSON.
2. Stage-local generations and cancelled/failed/stale exclusion. Decode measures
   parent process launch and started-through-validated-pixels IPC completion, including
   worker admission/probe/transport. CPU upload/render submission and Qt frame-swap
   are explicitly not GPU completion, codec-only CPU or physical presentation.
3. Replay lifetime/deadlines, real cache hit/miss and failure/cancellation paths.
   Normal launches never construct it; it dispatches no mutations or shell commands.
4. Generator containment, complete independent fixture verification, raw-distribution
   aggregation (no percentile averaging or cross-process timestamp merge), comparer
   rejection guards and isolated software-GL container lifecycle.
5. Both test-only prerequisites above; production QML/policy remains outside Codex.

## Evidence

Full commands and actual output: docs/verification/PERF-MEASURE-01.md.
Required Windows full candidate 3885ca7 run passed 15/15 in 184.75s. Earlier timeouts
remain documented. Historical native 50-capture and Linux 50-capture replays pass;
Linux 16,527 spans, zero drops. Final native full-dataset session passes 559 spans,
zero drops and all source hashes unchanged. Strict full dataset checks 111,136 exact
files/paths/dimensions/hashes. No original media was used or modified.

The prior Linux full gate failed only platform-launcher environment assumptions.
Initial new process-routing test passed 1/1 in 0.37s before the final extra literal-path
case. Required combined affabf4 full commands passed: Windows 15/15 in 203.16s;
Docker 15/15 in 128.24s. The required runtime image also passed complete smoke replay,
source hashes unchanged. The lead releases this combined candidate for review. No test is skipped or
replaced with unconditional launch success to manufacture that gate.

Physical HDD, SMB, DPI/ICC/HDR/high-refresh, active GPU identity, large-MP decode and
100k-folder interactive latency remain unqualified. Software/WSL bind-mounted replay
and uncontrolled power/cache/thermal state are not hardware qualification.

Remaining: actual Claude approval, serial main
integration and required checks on the integrated commit. PLATFORM-01 is frozen on
main but starts only after PERF reaches review. Do not self-approve or mark done.
