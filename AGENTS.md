# Glancelot agent instructions

## Operating model (user decision, October 22, 2026)

The three-lane model (Claude, Codex, Gemini) is retired; docs/AGENT-LANES-PLAN.md and the lane rules in docs/handoff/LEAD-TAKEOVER-2026-10-22.md are history. The current plan is docs/handoff/LEAD-PLAN-2026-10-22.md.

- **Lead (parent).** One Claude Code Opus 5.5 session owns every remaining task. It alone edits shared contracts (`include/glancelot/*`), root `CMakeLists.txt`, `cmake/*.cmake`, `docs/WORK-QUEUE.yaml`, `AGENTS.md` and `.github/workflows/*`, freezes contracts, assigns packets and reviews, pushes `gate/*` branches and integrates. A packet's queue row or brief may grant a track named lead-owned files; that grant covers only those files.
- **Track sessions.** One session per track or packet set. Each implements only its assigned packets, through subagents if it likes, on branch `claude/<task>` cut from the contract commit the lead names. Locally: worktree `.worktrees/claude-<task>`, build directory `build-claude-<task>`; never edit another session's worktree. A track never edits the queue, root CMake, shared headers or another track's owned files; it asks the lead in its handoff.
- **Review sessions.** A fresh-context reviewer that never implemented the packet reviews it from a read-only checkout at pinned SHAs (`git diff <contract SHA> <tip SHA>`) and writes `docs/verification/<TASK>-REVIEW.md` with APPROVED or CHANGES REQUIRED. A session never reviews its own packets. Fixes go back to the implementer; a new fresh reviewer re-reviews.
- **Local lead topology (user decision, October 22, 2026).** When the lead runs on the user's Windows PC it spans local and cloud work:
  - *Implementers* run as cloud sessions the lead starts (Linux builds, targeted and full offscreen `ctest`) and push `claude/<task>`. Cloud subagents are not available to the lead (probe, October 22: a remote-isolation subagent ran locally) and cloud sessions cannot be messaged back, so the lead relays a Windows failure by starting a fresh cloud fix session on the same branch with the failure text, or by asking the user to paste it into the running session.
  - *Windows compile feedback* for implementers comes from `.github/workflows/build.yml`, whose `windows-2022` `viewer` job builds with MSVC and runs the full suite on every push to every branch.
  - *Local reviewers* are fresh-context subagents on this PC, one per packet, in `.worktrees/review-<task>` with `build-review-<task>`. They review at pinned SHAs and may build and run the packet's targeted Windows tests, so an approval covers Windows behaviour.
  - *Local tester* is one subagent that runs every full gate, one at a time, on the exact commit to be merged: `scripts/build.ps1 -Test` and `docker build` in one gate-lock block, in `.worktrees/lead-tester`, with logs and QTest results files recorded under `docs/verification/`.
  - *Capacity:* at most three local builds at once, and none while a full gate holds the gate lock (the lead schedules this, because overlapping builds break the timing-budget tests).
- **Gate.** A full Windows `scripts/build.ps1 -Test` plus `docker build` on the user's PC, under the gate lock, on the exact commit to be merged, decides done. It is run by the local tester when the lead is local, or by the CI job `lead-windows-gate (authoritative)` in `.github/workflows/windows-gate.yml` (self-hosted runner) when the lead is in the cloud or the tester is unavailable. Only the lead pushes `gate/<name>`. The `hosted-windows (advisory)` job and `build.yml` are early warnings; a failure seen only there is triaged against a full gate. Runbook: docs/CI-GATE-RUNNER.md. Never add a `pull_request` trigger to a workflow that can reach the self-hosted runner (public repository).
- **Handoff.** Every packet ends with `docs/handoff/<TASK>-HANDOFF.md`: branch and tip SHA, files changed, actual `ctest` output (targeted `ctest -R` first, then the full offscreen suite), failure and cancellation cases covered, anything unverified.
- **Build questions (user decision, October 22, 2026).** Child sessions and subagents do not stop to ask the user build or implementation questions. They choose what gives the best performance and user experience within the packet's contract, record the choice and the alternatives in the handoff under "Decisions taken", and keep going. A question that changes a frozen contract, product behaviour the user sees, or data safety goes to the lead: local agents message the lead; cloud sessions list it under "Open questions for the lead" and finish everything else. The lead answers, and brings the user only real product choices.
- **Pre-handoff checklist.** Every handoff includes, with actual output:
  1. *Fail-before evidence* for every new or changed assertion: show it fails against the base (or with the fix reverted). A check that cannot fail is a defect.
  2. *Grant check:* `git diff --name-only <base>..<tip>` compared against the files the packet grants; anything outside is listed and justified.
  3. *Exact base:* the base SHA, and for a stacked packet the exact parent commit it sits on (the parent's tip, not an earlier commit).
  4. Targeted `ctest -R`, then the full offscreen suite, with baseline failures named.
  5. "Decisions taken" and "Open questions for the lead".
- **Reviews write their document first.** Every review, stage 1 included, commits `docs/verification/<TASK>-REVIEW*.md` on `claude/review-<task>` and pushes it before reporting. Fix briefs point at that document instead of restating it.
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
