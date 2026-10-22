# Glancelot agent instructions

## Operating model (user decision, October 22, 2026)

The three-lane model (Claude, Codex, Gemini) is retired; docs/AGENT-LANES-PLAN.md and the lane rules in docs/handoff/LEAD-TAKEOVER-2026-10-22.md are history. The current plan is docs/handoff/LEAD-PLAN-2026-10-22.md.

- **Lead (parent).** One Claude Code Opus 5.5 session owns every remaining task. It alone edits shared contracts (`include/glancelot/*`), root `CMakeLists.txt`, `cmake/*.cmake`, `docs/WORK-QUEUE.yaml`, `AGENTS.md` and `.github/workflows/*`, freezes contracts, assigns packets and reviews, pushes `gate/*` branches and integrates. A packet's queue row or brief may grant a track named lead-owned files; that grant covers only those files.
- **Track sessions.** One session per track or packet set. Each implements only its assigned packets, through subagents if it likes, on branch `claude/<task>` cut from the contract commit the lead names. Locally: worktree `.worktrees/claude-<task>`, build directory `build-claude-<task>`; never edit another session's worktree. A track never edits the queue, root CMake, shared headers or another track's owned files; it asks the lead in its handoff.
- **Review sessions.** A fresh-context reviewer that never implemented the packet reviews it from a read-only checkout at pinned SHAs (`git diff <contract SHA> <tip SHA>`) and writes `docs/verification/<TASK>-REVIEW.md` with APPROVED or CHANGES REQUIRED. A session never reviews its own packets. Fixes go back to the implementer; a new fresh reviewer re-reviews.
- **Gate.** The CI job `lead-windows-gate (authoritative)` in `.github/workflows/windows-gate.yml`, on the user's self-hosted runner, is the only gate that decides done: full Windows `scripts/build.ps1 -Test`, then `docker build`. Only the lead pushes `gate/<name>`. `hosted-windows (advisory)` is an early warning; a failure seen only there is triaged against the authoritative job. Runbook: docs/CI-GATE-RUNNER.md. Never add a `pull_request` trigger (public repository, self-hosted runner).
- **Handoff.** Every packet ends with `docs/handoff/<TASK>-HANDOFF.md`: branch and tip SHA, files changed, actual `ctest` output (targeted `ctest -R` first, then the full offscreen suite), failure and cancellation cases covered, anything unverified.
- **Budget tests** (`fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui`) can flake under load outside the gate machine: rerun once and record both results. The authoritative gate decides.

## Hot files

- The lead names, per integration cycle, which packet may edit `ui/Main.qml` and `src/app/viewer_controller.{cpp,hpp}`, and which region of them (the current cycle's grants are in its cycle document under docs/handoff/). Everyone else adds a component file or a library class under `src/app/` with a thin call site, and the lead resolves the include or instantiation line.
- New commands go in a per-task block of `src/app/command_registry.cpp`; edit only your own block.
- New UI tests go in a new per-feature test file (`tests/<feature>_ui_tests.cpp`), never `tests/grid_ui_tests.cpp`. New test targets go in a lead-provided `cmake/<Feature>Tests.cmake`.
- Track ownership of other files is in docs/handoff/LEAD-PLAN-2026-10-22.md, section 2.

## Engineering rules

- Build C++20 / Qt 6.8.3 Quick with a GPU canvas. Preserve docs/REQUIREMENTS.md and its acceptance criteria.
- Use the relevant ui-design-skills when designing, implementing or reviewing UI. Read application-interface-design plus applicable layout, spacing, typography, geometry, iconography, motion and responsive skills; use design-review for substantial UI changes. Apply their principles to native Qt/QML, preserve supplied branding and theme tokens, and do not introduce web frameworks solely because a skill uses web examples. Include this requirement in delegated UI tasks. If the skills are not available in a session, say so in the handoff.
- Freeze interface changes with the lead before changing a shared header. No stable external plugin ABI.
- Async results carry a generation/revision; stale results cannot replace a newer selection. Bound allocation, queues and decoder lifetime.
- UI/render code never performs image decoding or blocking directory scans. Viewing never writes original media.
- Use disposable/generated test media. Include failure/cancellation cases and actual command output in handoffs.
- Task states: ready -> implementing -> review -> integration -> done. A task is done only after independent review and a passing authoritative gate on the integrated commit.
- Use `scripts/build.ps1 -Test` on Windows; CMake/CTest commands, including Linux, are documented in BUILDING.md. Qt is project-local under .tools/Qt.
- No cloud, telemetry, mandatory catalog, automatic updates, publishing or paid infrastructure in the viewer.
- Report hardware/platform tests as unverified when unavailable. Physical qualification is done by the user from RUNBOOK packets; no headless result is relabeled as physical.

## Gate lock (this Windows machine)

Only one full `scripts/build.ps1 -Test` or `docker build` runs on this machine at a time, because overlapping runs break timing-budget tests.

- The lock is machine-local, in the shared git directory, and is never written to the queue. `scripts/build.ps1 -Test` takes it automatically; the CI gate job takes it too.
- Run Docker and any older worktree's build through the main checkout's script, in one PowerShell process: `& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch <branch> -Run { <command> }`. Check it with `-Status`.
- Targeted `ctest -R` runs are exempt, except `folder_grid`, `fs_watch_budget` and clipboard UI tests while a CI gate holds the lock. Run nothing heavy on this machine while the runner is gating.

## Integration (lead only)

Integration is serial. Set `integration_lock` in docs/WORK-QUEUE.yaml, merge `main` into the reviewed branch (or batch 2-4 approved packets into `lead/integrate-<track>-<n>`), push that exact commit as `gate/<name>`, wait for `lead-windows-gate` to pass, merge `--no-ff`, record the run URL, summary and downloaded logs, update the queue, clear the lock, push `main` and delete the `gate/*` branch. A failing packet is pulled from the batch and returned.

## Memory

- Repository documents and Git are authoritative. Cloud sessions cannot reach AgentMemory; for them the handoffs, reviews and queue are the shared memory.
- Local sessions recall relevant AgentMemory context/actions at task start, without permission. For multi-step work create/update an action; save important decisions and mark completed actions done with evidence.
- AgentMemory MCP: memory_smart_search / memory_recall; memory_action_create / memory_action_update; memory_save / memory_lesson_save. Server: http://192.168.86.49:3111.
