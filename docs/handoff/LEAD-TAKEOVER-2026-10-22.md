# Lead takeover and remaining-work plan, October 22, 2026

Written for: the Claude lead session that continues this work (local or cloud), and the user.

## 1. Decisions (user, October 22, 2026)

- **One lead, all lanes.** The Claude Code Opus 5.5 session is lead and integrator, and owns every remaining task regardless of lane. The Codex and Gemini lanes are retired for this work. The lead implements through subagents and uses as many as the machine sustains.
- **Scope.** First correct and confirm the integrated Claude stack, then deliver v1 milestones M3 through M6 (inventory waves 3 to 8). Items marked conditional (C) or expansion-only get a feasibility packet first and are implemented only when that gate passes: F024, F039, F048, F071, F074, F076, F077, F078.
- **Review.** Cross-lane review is replaced by fresh-context review. Every packet is reviewed by a separate Claude subagent that did not implement it, starts with no implementation context, works read-only in its own worktree, and pins exact SHAs. A reviewer never approves a packet it wrote or fixed.
- **Models.** Opus 5.5 implements and reviews.
- **Branches.** On October 22, 2026 the user directed "merge all branches". The stack and the review/verification evidence branches were merged to `main` (section 2). Superseded and retired branches were not, and pushing to GitHub was authorized for the cloud handoff.

Every other AGENTS.md rule still applies: requirements and acceptance, ui-design-skills for UI, generation fencing, bounded work, no decode or blocking scans on the UI thread, generated test media, actual command output in handoffs, and unverified physical gates.

## 2. What was merged on October 22

| Merged | Notes |
| --- | --- |
| `claude/f051` (contains F002, F037, F031, F040, F049, F009, F014, F051, plus F053, F052, F054, F015 and F042 type chips) | Five conflicts resolved, keeping main's monitor-customization APIs and adding the stack's. Details are in the merge commit message. |
| Review and verification branches (`claude/review-*`, `claude/verify-*`, `codex/review-*`, `agent/review-f0xx`, `claude/stack-sync-2026-10-22`, `claude/audit-ux`) | Only their documents were merged. Code they carried was reset to `main`'s version, because it was older copies of the stack or red review probes. |
| **Not merged:** `claude/input-02-impl`, `claude/input-02-mainsync`, `claude/input-02-snapshot` | Superseded; INPUT-02 is integrated. |
| **Not merged:** `codex/platform-native-fixture` | The retired fixture that Defender flagged. Never restore it (queue `windows_native_fixture_hold`). |

The integration evidence is in `docs/verification/lead-integrate-all-2026-10-22/README.md`. It holds the exact commands and results that were run before the push. The full Windows and Docker gates on the merged commit are part of Wave A.

## 3. Execution model

**Per wave:**

1. **Contract commit (lead).** On `main`, the lead commits everything the wave shares:
   - the shared headers;
   - the CMake test registrations (`cmake/<Feature>Tests.cmake` plus the `include()` line);
   - an empty per-task block in `src/app/command_registry.cpp` for each packet;
   - one `docs/<TASK>-PLAN.md` per packet;
   - the queue entries.

   Implementers never edit root CMake, the queue, or another packet's headers. A change to a frozen header comes back to the lead.
2. **Implementers.** One subagent per packet. Each uses:
   - worktree `.worktrees/claude-<task>`;
   - branch `claude/<task>`, created from the contract commit;
   - build directory `build-claude-<task>`.

   Each implementer brief includes the AGENTS.md ui-design-skills requirement for UI work. Tests go in per-feature files (`tests/<feature>_tests.cpp` or `tests/<feature>_ui_tests.cpp`). Each implementer runs targeted `ctest -R` and writes a handoff with actual output.
3. **Reviewers.** For each packet, a fresh subagent reviews `git diff <contract SHA> <tip SHA>` in `.worktrees/claude-review-<task>` and writes `docs/verification/<TASK>-REVIEW.md` with APPROVED or CHANGES REQUIRED. Fixes go back to an implementer, then a new fresh reviewer re-reviews.
4. **Integration (lead, serial).**
   1. Set `integration_lock`.
   2. Merge `main` into the approved branch, or batch two to four approved packets in `lead/integrate-<wave>-<n>`.
   3. Run the full gate once, then merge with `--no-ff`.
   4. Record the evidence and clear the lock.
   5. A failing packet is pulled from the batch and goes back to its implementer.

**Ordering hot files.** Only one packet per wave edits `ui/Main.qml` directly. Other packets add QML components plus a single include line, and the lead resolves those lines at integration.

**Concurrency.**
- **This Windows machine:** 20 threads, about 4 GB free RAM, and each build uses `--parallel 4`, so run at most 4 compiling agents at once, plus read-only reviewers.
- **Cloud:** size to the instance; 6 to 8 is reasonable if RAM allows about 2 GB per compile job.
- **Full gates** run one at a time on any host. These tests carry wall-clock budgets and fail under load: `fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller` and `input_ui`. Targeted `ctest -R` runs may overlap.

### Running in the cloud

- **Build.**
  - `scripts/build.ps1` is Windows/MSVC only, and Qt is not in the repo (`.tools/` is gitignored).
  - Probe first: `docker version`. If Docker works, the Linux gate is `docker build -t glancelot-browser:main .`, which is the Dockerfile's `build` stage: Ninja, then all tests offscreen.
  - Otherwise install Qt 6.8.3 gcc_64 with aqtinstall, as the Dockerfile's `qt` stage does, and use the CMake/CTest commands in BUILDING.md with `QT_QPA_PLATFORM=offscreen`.
  - Do not assume either is available; check.
- **Gate lock.** `scripts/gate-lock.ps1` needs PowerShell. In Linux, serialize full gates yourself and still never overlap two.
- **AgentMemory** (`192.168.86.49`) is a LAN address and unreachable from the cloud. Repo documents are the only memory, so record decisions in handoffs and the queue.
- **Windows gate (user decision: option 3, self-hosted runner).** Before merging to `main`, push the exact candidate commit as `gate/<task>-<n>`. The `windows-gate` workflow then runs the full Windows gate and the Docker gate on the lead's Windows machine under the machine gate lock. Merge only after that run passes, and record its URL, summary and logs. The procedure is in [CI-GATE-RUNNER.md](../CI-GATE-RUNNER.md). If no runner is online, the gate is pending: merge nothing as done, leave packets in `integration` with `windows_gate: pending`, and keep working on other packets. The native-fixture hold (merge 334065e) still applies, and the workflow enforces it.
- **Pushing (standing user approval, October 22, 2026).** The lead may push without asking: packet, review, `gate/*` and integration branches, and fast-forward pushes of `main` after a gated integration. Never force-push, and never rewrite history that is already pushed. Delete `gate/*` branches once their results are recorded.

## 4. Waves

Each packet ID below becomes a queue task. The explorer reports that informed this plan did not finish locally, so **step 0 of Wave B is to re-run the inventory mapping**: existing code per item, hot files and dependencies, against `docs/FEATURE-OPPORTUNITIES.md`, `docs/PERFORMANCE-STRATEGY.md` and `docs/REQUIREMENTS.md`. Adjust packet boundaries before freezing contracts.

### Wave A: stack correction and confirmation (frozen in LEAD-WRAPUP-2026-10-22.md)

Four implementers in parallel, then one confirmation reviewer:

| Packet | Scope |
| --- | --- |
| STACK-F015-RECOVERY | Tabs persist through the production recovery session. Extend `recovery::CheckpointInput` and `ResumeState` with defaulted, value-owned tab state (at most 8 records plus an active index), written atomically with this slot's checkpoint revision. Read legacy entries without tabs; no automatic restore under SAFE or reset; never clean before the newest complete snapshot. Tests: restart, old entry, malformed/oversized, stale sequence, busy/failure/retry, other-slot preservation, SAFE/reset. Submit the exact header diff for review. |
| STACK-F040-PINNED | The F040 reference window and the MONITOR-OUTPUT pinned output stay distinct commands. Both go through the shared pixel/texture admission and release accounting, with no duplicate decode and no change to the selected image. Parity tests: close, pin replacement, recovery while both are active. |
| STACK-F049-PRESETS | Presets obey the canonical lock and frozen-pane rules and keep per-slot placement isolated. `snapshotExtras_` and unknown or newer preference data survive `persistWhole()`. Named layout presets and monitor workspace presets have distinct names and scopes in the UI. The navigator follows `paneAvailable("preview")` when the preview is detached. |
| STACK-F054-COVERAGE | Wrap main's newer strings: MetadataHud, AnimationBar, RenameFlow, TransferHud, the presentation/settings/monitor dialogs and DetachedPaneHost. Preserve interpolation and accessibility names. RTL for detached windows. Pseudo-locale, clipping and RTL screenshots. Fix `ShortcutCheatSheet.qml` `ReferenceError: keys is not defined`. |
| STACK-CONFIRM | Fresh reviewer: whole-stack confirmation on integrated `main`, including the merge resolutions in `3b2b0db`. Then the full Linux gate, plus the Windows gate wherever available. |

Close PATH-COMPLETE-01 and VIEW-POLISH-01, and add queue rows for F031, F040, F049, F009, F014, F051, F053, F052, F054, F015 and F042 chips with their evidence.

### Wave B: browsing core (M3)

- PERF-ENUM + F016: incremental listing, stable anchors.
- PERF-THUMB: DPR size buckets, shared visible thumbnails, cancellation of hidden panes.
- F042 core: capture-date and dimension sorts through the metadata service.
- F013: group headers.
- F007: scope and status controls.
- F008: jump to index, name or date.
- F061: structured recovery UX.

### Wave C: culling and review (M4)

- F025 sidecar/XMP conflict engine, then the pick/reject/stars/labels UI.
- F022: asset grouping.
- F028: burst engine and stack UI.
- F029: culling progress.
- F026 + F027: 2-up and 4-up compare; blink, wipe and difference.
- F012 + F030: basket, collections and manifest format.

### Wave D: organization and handoff (M4–M5)

- F019 batch-rename engine and preview UI.
- F017 + F062: operation journal, undo, operation center.
- F021: drag and drop, with keyboard alternatives.
- F066: open-with and tool slots, plus their settings UI.
- F068: shell associations and single-instance IPC.
- F069: command-line entry points.
- F070 UI: the export dialog on the integrated engine. Its worker must contain engine exceptions (review O1).
- F005 + F006: recursive search and saved recipes.
- F023: exact duplicates, review-only.
- F057 UI, if the restore UI is still missing.

### Wave E: viewing and color (M5)

- PERF-DECODE + F033: progressive tiers.
- PERF-RENDER: texture reuse and GPU-loss recovery.
- F043: capability model and status.
- F044: per-monitor ICC. Software pipeline only; physical accuracy is unverified.
- F038: sampling, histogram and overlays.
- F034: tiled viewing.
- F047: lossless JPEG transforms.
- F046 + F032 export engines and UI.
- F045: AI-metadata parsers.

### Wave F: workspace, system and accessibility (M6)

- F052 and F053 completion.
- PERF-RESOURCE.
- PERF-POWER + F059.
- F058: diagnostics backend and panel, redacted, no upload.
- F060: cache controls.

### Wave G: remote, container and hardening (M6)

- PERF-CONTAINER + F079: deployment self-check.
- PERF-REMOTE + F073: remote quality and responsiveness, plus status UI.
- F075: server-path and read-only indicators.
- F072: GPS handoff, user-invoked only.
- PERF-WINDOWS and PERF-LINUX matrix scripts.
- PERF-SOAK: soak and fuzz harnesses.
- MONITOR-VERIFY-01 harness and evidence packet. Physical rows stay unverified unless measured on real hardware.
- The final five-workflow UX retest (FEATURE-OPPORTUNITIES §14).

### Feasibility only, until their gates pass

F024, F039, F048, F071, F074, F076, F077, F078. Each gets a `docs/<ID>-FEASIBILITY.md` covering dependencies, licensing, platform capability, security model and test strategy. The lead promotes an item to a packet only if the gate passes.

### Physical qualification, which agents cannot close

CANVAS-UI and DECODE-01 physical DPI and performance, and the MONITOR-VERIFY physical matrix: mixed DPI and refresh, ICC/HDR, hotplug, suspend, GPU loss, native Wayland/X11, NVDA/Orca. These stay `integration` or `unverified` with explicit limits.

## 5. Commits

- **Contract commit per wave:** `chore(lead): freeze <wave> contracts`.
- **Packet branches:** Conventional Commits (`feat(<area>):`, `fix(<area>):`, `test(<area>):`).
- **Integration:** merge with `--no-ff`, then a `docs: qualify <packet> integration` evidence commit.
- **Queue:** updates only at freeze, integration and closeout. Validate with a strict-duplicate YAML parse.

## 6. Housekeeping

Locally there are 132 worktrees using about 21 GB, and 79 of their branches are merged. After this push, the lead may prune worktrees whose branches are merged into `main` (`git worktree remove`, then delete the merged branch). Never prune unmerged or unpushed work.
