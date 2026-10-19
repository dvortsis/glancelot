# Glancelot agent instructions

- Build C++20 / Qt 6.8.3 Quick with a GPU canvas. Preserve docs/REQUIREMENTS.md and its acceptance criteria.
- Use the relevant ui-design-skills when designing, implementing or reviewing UI. Read application-interface-design plus applicable layout, spacing, typography, geometry, iconography, motion and responsive skills; use design-review for substantial UI changes. Apply their principles to native Qt/QML, preserve supplied branding and theme tokens, and do not introduce web frameworks solely because a skill uses web examples. Include this requirement in delegated UI tasks.
- Lead work (shared contracts, root CMake/configuration, docs/WORK-QUEUE.yaml and integration) is done only by the session currently acting as lead or integrator, under the integration lock below. Implementation and review agents never do it.
- Every implementation task uses its assigned worktree, branch and build directory. Never edit another agent's worktree.
- Three agent lanes work concurrently: Claude Code (`claude/*`), Codex (`codex/*`) and Gemini (`agent/*`). docs/AGENT-LANES-PLAN.md assigns tasks to lanes; never take another lane's task. Each lane has at most one active implementation task. Reviews cross lanes: a reviewer never comes from the implementing lane and never approves its own work.
- Lane naming: worktree `.worktrees/<lane>-<task>`, build directory `build-<lane>-<task>`. Gemini keeps `agent/<task>` and `.worktrees/<task>`.
- Hot files: per wave, only the lane that owns its packet edits `ui/Main.qml`. New commands go in a per-task block of the command table. New UI tests go in a new per-feature test file, not `tests/grid_ui_tests.cpp`.
- Integration is serial. Set `integration_lock` in docs/WORK-QUEUE.yaml before merging, merge `main` into the reviewed branch, run `scripts/build.ps1 -Test` plus `docker build -t glancelot-browser:main .`, merge `--no-ff`, record the evidence, then clear the lock. Never merge while another holder has the lock. A branch that conflicts goes back to its lane.
- Freeze interface changes with the lead before changing a shared header. No stable external plugin ABI.
- Async results carry a generation/revision; stale results cannot replace a newer selection. Bound allocation, queues and decoder lifetime.
- UI/render code never performs image decoding or blocking directory scans. Viewing never writes original media.
- Use disposable/generated test media. Include failure/cancellation cases and actual command output in handoffs.
- Task states: ready -> implementing -> review -> integration -> done. A task is done only after independent review and checks on the integrated commit.
- Use `scripts/build.ps1 -Test` on Windows; CMake/CTest commands are documented in BUILDING.md. Qt is project-local under .tools/Qt.
- Recall relevant AgentMemory context/actions at task start, without permission. For multi-step work create/update an action; save important decisions and mark completed actions done with evidence.
- AgentMemory MCP: memory_smart_search / memory_recall; memory_action_create / memory_action_update; memory_save / memory_lesson_save. Server: http://192.168.86.49:3111.
- Repository documents and Git are authoritative. Memory supplements them. Report hardware/platform tests as unverified when unavailable.
- No cloud, telemetry, mandatory catalog, automatic updates, publishing or paid infrastructure in the viewer.
