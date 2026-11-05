# Glancelot agent instructions

## Operating model (user decision, October 22, 2026)

The three-lane model (Claude, Codex, Gemini) is retired; docs/AGENT-LANES-PLAN.md and the lane rules in docs/handoff/LEAD-TAKEOVER-2026-10-22.md are history. The current plan is docs/handoff/LEAD-PLAN-2026-10-22.md.

- **Lead (parent).** One Claude Code Opus 5.5 session owns every remaining task. It alone edits shared contracts (`include/glancelot/*`), root `CMakeLists.txt`, `cmake/*.cmake`, `AGENTS.md`, `.github/workflows/*`, `scripts/ci/*`, `scripts/lead-*` and everything in `docs/WORK-QUEUE.yaml` except a packet's own row, freezes contracts, assigns packets and reviews, pushes `gate/*` branches, integrates and is the only session that merges. A packet's queue row or brief may grant a track named lead-owned files; that grant covers only those files.
- **Track sessions.** One session per track or packet set. Each implements only its assigned packets, through subagents if it likes, on branch `claude/<task>` cut from the contract commit the lead names. Locally: worktree `.worktrees/claude-<task>`, build directory `build-claude-<task>`; never edit another session's worktree. A track edits only its own row of the queue (never a top-level key or another row), and never root CMake, shared headers or another track's owned files; it asks the lead in its handoff. Nobody pushes to another session's branch: to change someone else's PR, message that session. The two exceptions: the lead may merge `main` into a branch in the landing lane, and a fix session may take over the branch of a finished cloud session, which cannot be messaged.
- **Review sessions.** A fresh-context reviewer that never implemented the packet reviews it from a read-only checkout at pinned SHAs (`git diff <contract SHA> <tip SHA>`) and writes `docs/verification/<TASK>-REVIEW.md` with APPROVED or CHANGES REQUIRED. A session never reviews its own packets. Fixes go back to the implementer; a new fresh reviewer re-reviews. Reviewers run on Opus.
- **Local lead topology (user decision, October 22, 2026).** When the lead runs on the user's Windows PC it spans local and cloud work:
  - *Implementers* run as cloud sessions the lead starts (Linux builds, targeted and full offscreen `ctest`) and push `claude/<task>`. Cloud subagents are not available to the lead (probe, October 22: a remote-isolation subagent ran locally) and cloud sessions cannot be messaged back, so the lead relays a Windows failure by starting a fresh cloud fix session on the same branch with the failure text, or by asking the user to paste it into the running session.
  - *Windows compile feedback* for implementers comes from `.github/workflows/build.yml`, whose `windows-2022` `viewer` job builds with MSVC and runs the full suite on every push to a pull request (one retry per test; budget tests are advisory there and enforced by the full gate). Open a draft PR with the first push.
  - *Local reviewers* are fresh-context subagents on this PC, one per packet, in `.worktrees/review-<task>` with `build-review-<task>`. They review at pinned SHAs and may build and run the packet's targeted Windows tests, so an approval covers Windows behaviour.
  - *Local tester* is one subagent that runs every full gate, one at a time, on the exact commit to be merged: `scripts/build.ps1 -Test` and `docker build` in one gate-lock block, in `.worktrees/lead-tester`, with logs and QTest results files recorded under `docs/verification/`.
  - *Capacity:* at most four child sessions or subagents at once (user decision, November 4, 2026), counting implementers, reviewers and the tester. Launch the next ready slice whenever a slot is free, unless the lead records a reason (see "Slots" below). At most three local builds at once, and none while a full gate holds the gate lock (the lead schedules this, because overlapping builds break the timing-budget tests). Long-running sessions run in Auto mode, never Plan, so nothing stalls on an approval nobody is watching.
- **Gate.** A full Windows `scripts/build.ps1 -Test` plus `docker build` on the user's PC, under the gate lock, on the exact commit to be merged, decides done. It is run by the local tester when the lead is local, or by the CI job `lead-windows-gate (authoritative)` in `.github/workflows/windows-gate.yml` (self-hosted runner) when the lead is in the cloud or the tester is unavailable. Only the lead pushes `gate/<name>`. The `hosted-windows (advisory)` job is an early warning; a failure seen only there is triaged against a full gate. `build.yml` feeds the required `ci-ok` check: its failure blocks a merge unless it is handled as a known red check (see "Pull requests and merging"). Runbook: docs/CI-GATE-RUNNER.md. Never add a `pull_request` trigger to a workflow that can reach the self-hosted runner (public repository).
- **Handoff.** Every packet ends with `docs/handoff/<TASK>-HANDOFF.md`: branch and tip SHA, files changed, actual `ctest` output (see the pre-handoff checklist), failure and cancellation cases covered, anything unverified.
- **Build questions (user decision, October 22, 2026).** Child sessions and subagents do not stop to ask the user build or implementation questions. They choose what gives the best performance and user experience within the packet's contract, record the choice and the alternatives in the handoff under "Decisions taken", and keep going. A question that changes a frozen contract, product behaviour the user sees, or data safety goes to the lead: local agents message the lead; cloud sessions list it under "Open questions for the lead" and finish everything else. The lead answers, and brings the user only real product choices.
- **Pre-handoff checklist.** Every handoff includes, with actual output:
  1. *Fail-before evidence* for every new or changed assertion: show it fails against the base (or with the fix reverted). A check that cannot fail is a defect.
  2. *Grant check:* `git diff --name-only <base>..<tip>` compared against the files the packet grants; anything outside is listed and justified.
  3. *Exact base:* the base SHA, and for a stacked packet the exact parent commit it sits on (the parent's tip, not an earlier commit).
  4. Targeted `ctest -R`, with baseline failures named. On this PC, do not run the full suite: the PR's hosted `viewer` job covers it, and the tester gates the exact head before merge. A cloud session without local gates still runs the full offscreen suite. Do not push red.
  5. "Decisions taken" and "Open questions for the lead".
- **Reviews write their document first.** Every review, stage 1 included, commits `docs/verification/<TASK>-REVIEW*.md` on `claude/review-<task>` and pushes it before reporting, comments the verdict on the PR, and on APPROVED posts the `fresh-review` status on the reviewed head (see "Pull requests and merging"). Fix briefs point at that document instead of restating it.
- **Budget tests** (`fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui`) can flake under load outside the gate machine: rerun once and record both results. The authoritative gate decides.

## Hot files

- The lead names, per integration cycle, which packet may edit `ui/Main.qml` and `src/app/viewer_controller.{cpp,hpp}`, and which region of them (the current cycle's grants are in its cycle document under docs/handoff/). Everyone else adds a component file or a library class under `src/app/` with a thin call site, and the lead resolves the include or instantiation line.
- New commands go in a per-task block of `src/app/command_registry.cpp`; edit only your own block.
- A command is handled in the component that owns it, in that component's `Connections { target: viewer }` handler (EN-07). `ui/Main.qml` keeps `invoke`, scope routing, and the commands that act on the canvas, the window or Main's own dialogs and modes. `handleViewCommand` is the list. Within one file, `Connections` handlers run in reverse declaration order (measured on Qt 6.8.3). So the `Connections` that calls `handleViewCommand` is declared last, and the loupe ends before a component's handler runs (`loupeEndsBeforeAMovedHandlerActs`). A handler inside a component that isn't loaded yet misses the command that would create it. So a command whose owner loads lazily is handled on the `Loader` in `Main.qml`, as `export-selection` is.
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
- `scripts/build.ps1 -Test` is the Windows gate command; targeted runs are in the pre-handoff checklist. CMake/CTest commands, including Linux, are documented in BUILDING.md. Qt is project-local under .tools/Qt.
- No cloud, telemetry, mandatory catalog, automatic updates, publishing or paid infrastructure in the viewer.
- Report hardware/platform tests as unverified when unavailable. Physical qualification is done by the user from RUNBOOK packets; no headless result is relabeled as physical.

## Gate lock (this Windows machine)

Only one full `scripts/build.ps1 -Test` or `docker build` runs on this machine at a time, because overlapping runs break timing-budget tests.

- The lock is machine-local, in the shared git directory, and is never written to the queue. `scripts/build.ps1 -Test` takes it automatically; the CI gate job takes it too.
- Run Docker and any older worktree's build through the main checkout's script, in one PowerShell process: `& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch <branch> -Run { <command> }`. Check it with `-Status`.
- Targeted `ctest -R` runs need only a build slot, not the gate lock, except budget tests and measurements that need a quiet machine, which take the gate lock. Do not run `folder_grid`, `fs_watch_budget` or clipboard UI tests while a gate holds the lock. Run nothing heavy on this machine while the runner is gating.

## Pull requests and merging (user decision, November 4, 2026)

Branch protection on `main` enforces the first rule and the `ci-ok` check for everyone, administrators included. The rest are rules for every session. Background: docs/decisions/2026-11-04-merge-protections.md.

**Branches**

- No direct pushes to `main`, docs fixes included. Every change goes through a pull request.
- One session per branch.
- Do not rebase a shared branch or force-push it. Merge `main` in only when needed (a conflict, the lead asks, or review needs a fresher head), and never while your own CI is running.
- Packet branches are `claude/<task>`, lead branches `lead/<name>`, and commit subjects and PR titles follow Conventional Commits, `type(scope): description` (docs/RELEASING.md). A packet PR's title ends with its task, as in `feat(grid): incremental folder listing (A1)`, and packet commits carry a `Task: <TASK-ID>` trailer. Lead bookkeeping uses `docs(queue): ...` or `docs(evidence): ...`. `guard` enforces the format for commits from November 6, 2026 and for titles from PR 19 on; PRs 18 and earlier may keep `<TASK-ID>: ` subjects.
- In a shallow clone, run `git fetch origin main` before pushing.

**Merging**

- Only the lead merges, with `scripts/lead-merge.ps1 -Pr <n>`, which pins the merge to the checked head (`-Auto` falls back to GitHub auto-merge for PRs without code, since `main` can move during the wait). Other sessions never merge.
- The merge gate:
  - `ci-ok` is green. It runs on every PR. Docs-only PRs get it from the `guard` job alone.
  - A fresh Opus reviewer found nothing blocking and posted `fresh-review` on the head. The lead posts it instead on a head that only merges reviewed work and `main` without conflicts, naming the reviews; a conflict resolution in code needs a reviewer.
  - Code changes passed the full gate (above) on that head, and `main` holds no code that head lacks: if it does, merge `main` in and gate again. The tester posts `local-gate`; when `lead-windows-gate` ran the gate, the lead posts it after checking the run and names the run URL.
  - Docs-only and CI-tooling PRs (`.github/*`, `scripts/ci/*`, `scripts/lead-*`) need no `local-gate`. Deleting or moving root `README.md` or `THIRD_PARTY_NOTICES.md` counts as code, because the packaging copies them.
  - Lead bookkeeping (a `lead/` branch touching only the queue, handoffs, decisions or evidence) needs `ci-ok` only.
- Post a status with: `gh api -X POST repos/dvortsis/glancelot/statuses/<sha> -f state=success -f context=fresh-review -f description="<review document>"` (`context=local-gate` for the tester, describing the evidence folder). A new push needs new statuses.
- A failing check that is also red on `main`, or a known flake with a fix PR open, in code the PR does not touch, does not block. First rerun the failed jobs (`gh run rerun <id> --failed`). If it stays red, the lead says so in a PR comment, adds the `ci-known-red` label and reruns; `ci-ok` then passes with a warning. The label counts only on a rerun, so a new push must fail and be looked at again, and it never excuses a cancelled build. Only the lead adds that label, and removes it when the flake is fixed.

**Keeping PRs from colliding**

- Keep PRs to about 1,500 changed lines where practical (docs and generated catalogs do not count). The lead splits a packet estimated over about 3,000 lines before it starts. `guard` fails a larger PR unless its body has a `Large PR: <reason>` line.
- An integration batch carries only PRs whose own `guard` passed, and the lead's own commits on a batch branch use the Conventional Commits format, because `guard` doesn't check a batch's commit subjects (docs/RELEASING.md).
- Integration is serial, because the full gate is. The lead batches 2-4 approved packets into `lead/integrate-<n>` (or merges `main` into one reviewed branch), opens a PR, has the tester gate that exact head, posts `fresh-review` naming the packet reviews, and merges. A packet PR whose commits land this way closes as merged. A failing packet is pulled from the batch and returned. Evidence, queue updates and the review documents from `claude/review-*` follow in a `lead/` bookkeeping PR; review branches are never merged themselves.
- A reviewed PR that keeps needing `main` goes into the lead's landing lane: the lead merges `main` into it and lands it next.
- Push once per round of fixes. Each push cancels and restarts that PR's CI.
- A flake in someone else's test gets fixed once. Check for an open fix PR, or tell the lead first.

**Slots (user decision, November 4, 2026)**

- Launch the next ready slice whenever a slot is free, unless the lead records a reason. The only reasons are a file conflict with a running slice, the release branch, or the CI queue. Record it under `slot_holds` in `docs/WORK-QUEUE.yaml` with the slice and the reason, and remove it when the reason ends.
- A slice with an unmet dependency is not ready, and needs no hold.
- The lead keeps a one-hour recurring timer that checks all slots are busy if there is unblocked work, and fills or records any free slot. The timer lasts only for the lead's session and at most seven days, so set it again on resume or expiry. On a timer turn the app refuses to start a session, so launch a background subagent in its own worktree instead.

**Shared files**

- A packet PR edits only its own row of `docs/WORK-QUEUE.yaml`; `guard` enforces it. Progress is read from GitHub (`gh pr list`, `scripts/lead-inbox.ps1`), so rows need no routine edits.
- Machine capacity (gate lock, build slots) lives in the shared git directory, never in a tracked file.
- Never stop processes by name or script name: other sessions run the same tools on this machine. Stop only your own run, by its task ID or your own scratch path.
- Each decision gets its own file under `docs/decisions/`, one per PR.
- `.github/workflows/*`, `scripts/ci/*` and `scripts/lead-*` are lead-owned; `guard` and `scripts/lead-merge.ps1` refuse them from `claude/*` without the lead's `lead-grant` label. The merge script's check is the real one, because a pull request runs its own copy of `guard`.

**Before pushing**

- `python scripts/ci/pr_guard.py --base origin/main --head-ref <branch>` (needs PyYAML; add `--pr <n>` for a PR numbered 18 or below). It also checks commit subjects. The opt-in `commit-msg` hook (`git config core.hooksPath scripts/hooks`) checks them as you commit.
- Versions and releases follow docs/RELEASING.md. The only version is `project(... VERSION)` in root `CMakeLists.txt`, and only the lead prepares a release, after the user approves it.
- The `ctest` runs from the pre-handoff checklist.
- `translations/glancelot_en_XA.ts` regenerated if UI strings changed.
- No fixed sleeps in tests. Wait on a condition with a deadline of at least 10 s, such as `QTRY_VERIFY_WITH_TIMEOUT(condition, 10000)`. A fixed sleep is a blocking review finding, and `guard` fails added `QTest::qWait`, `sleep` calls and shorter explicit deadlines. It is best effort: plain `QTRY_*` (5 s default) and multi-line calls stay with the reviewer. Where the code really must let time pass (asserting that nothing fires, backoff in a fake, a hang loop in a child process), end the line with `// sleep-ok: <reason>`.

## Memory

- Repository documents and Git are authoritative. Cloud sessions cannot reach AgentMemory; for them the handoffs, reviews and queue are the shared memory.
- Local sessions recall relevant AgentMemory context/actions at task start, without permission. For multi-step work create/update an action; save important decisions and mark completed actions done with evidence.
- AgentMemory MCP: memory_smart_search / memory_recall; memory_action_create / memory_action_update; memory_save / memory_lesson_save. Server: http://192.168.86.49:3111.
