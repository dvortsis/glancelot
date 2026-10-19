# Codex lane: Wave 1 brief

Read this file first, then `AGENTS.md` and `docs/AGENT-LANES-PLAN.md`. Run one task at a time, in this order:
1. **PERF-MEASURE-01**
2. **PLATFORM-01**

The Codex lane owns every row marked "Codex" in the lanes plan. Never take Gemini-lane (`agent/*`) or Claude-lane (`claude/*`) tasks, and never edit their worktrees.

## Setup for each task

```powershell
cd C:\src\Github\Glancelot
git worktree add -b codex/perf-measure .worktrees/codex-perf-measure main
cd .worktrees/codex-perf-measure
./scripts/build.ps1 -BuildDir build-codex-perf-measure -Test   # finds the main checkout's .tools/Qt automatically
docker build -t glancelot-browser:codex-perf-measure .            # Linux build plus full CTest
```

- PLATFORM-01 uses branch `codex/platform-01`, worktree `.worktrees/codex-platform-01` and build directory `build-codex-platform-01`.
- Qt 6.8.3 for MSVC lives at `C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64`.

## Task 1: PERF-MEASURE-01

The contract is `docs/PERF-MEASURE-PLAN.md` together with the frozen header `include/glancelot/perf_trace.hpp`.
- Implement the header exactly.
- Make only the CMake, app and tool additions the plan pre-approves.
- If you need any other shared-header or root-configuration change, stop and write a proposed diff into your handoff instead.

## Task 2: PLATFORM-01 (start after PERF-MEASURE-01 reaches review)

Queue entry: PLATFORM-01 in `docs/WORK-QUEUE.yaml`. Scope:
- a Qt 6.8.3 Linux GUI build and a deployable package, built in Docker from the existing Dockerfile toolchain;
- Windows portable package evidence (`scripts/build.ps1 -Test -Package`);
- reproducible provenance: version, commit, Qt SBOM and executable hashes (F080, PERF-BUILD basics);
- qualification scripts and a matrix document for the open physical gates:
  - CANVAS-UI: DPI, backend and refresh;
  - DECODE-01: performance.

Ask the lead to freeze the contract and write `docs/PLATFORM-01-PLAN.md` before you start coding. Physical-hardware results stay "unverified" unless you actually measure them.

## Project rules (from AGENTS.md)

- **Async and bounds:** async results carry generations, and stale results never replace a newer selection. Allocation, queues and worker lifetime are bounded.
- **UI thread:** UI and render code never decode images or scan directories.
- **Source safety:** viewing never writes original media.
- **Tests:** use generated, disposable media, and include failure and cancellation cases.
- **No outside services:** no cloud, telemetry, automatic updates or paid infrastructure.
- **Hot files:**
  - Do not edit `ui/Main.qml` unless the packet says so.
  - Put new commands in a block of the command table commented with your task ID.
  - Put new tests in new test files.
- **Commits:** Conventional Commits, and no build logs in Git. Do not push.

## Handoff and review

When the task builds, passes the full suite on Windows and in Docker, and has `docs/verification/<TASK>.md` with real command output:
1. Set the task's state to `review` in your handoff message. Do not edit `docs/WORK-QUEUE.yaml` yourself; the lead or integrator does that.
2. Report the commit SHAs, the actual CTest summary lines, the targeted test output, and any deviations or unverified items.
3. A reviewer from the Claude Code lane reviews the branch in a separate worktree.
4. The integrator merges it under `integration_lock` (see AGENTS.md).

Address review findings on your own branch.

## Cross-lane review duty

When Claude-lane branches reach review, the Codex lane provides the independent reviewer. The first is `claude/input-02`. Review in a fresh worktree (`.worktrees/codex-review-<task>`) and build it yourself. Report APPROVED or CHANGES REQUIRED with numbered findings: severity, file:line and a concrete failure scenario. Include the actual test output. Never fix code in the branch under review.
