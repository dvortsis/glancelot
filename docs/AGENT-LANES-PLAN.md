# Agent lanes: Claude Code, Codex and Gemini

Lead decision, October 18, 2026, approved by the user. Three agent tools work concurrently on Glancelot. This document assigns every remaining task, feature opportunity and performance packet to the lane best suited to it, and defines how the lanes avoid colliding.

- **Gemini lane:** owns everything named in its own plan (`gemini_flash_feature_plan.md`), and other lanes leave those tasks alone. That covers:
  - DELETE-KEYS-01, PLATFORM-OPS-01 (F065), UI-SETTINGS-01 (including F050), FAVORITES-01 (F003), FILTER-01, CACHE-01 (PERF-CACHE), IO-01 (PERF-SCHED), META-HUD-01 (F041), FORMAT-02 (F035) and MONITOR-OUTPUT-01;
  - F001, F004, F010, F011, F018, F020, F036, F056 and F067.
- **Claude Code and Codex lanes:** everything else, as assigned below.
- **Authority:** docs/REQUIREMENTS.md, docs/WORK-QUEUE.yaml and the verification records remain authoritative. A row below becomes active only when the lead freezes its contracts and promotes it in the queue.

## Amendment, October 20, 2026 (lead decision, approved by the user)

- **Tools per lane.**
  - Claude lane: Claude Code Sonnet 5.5.
  - Codex lane: Codex 6.1 Sol.
  - Gemini lane: Antigravity 3.8 Flash.
  - Lead/integrator: the Claude Code Opus session, which implements no lane tasks.
- **Why.** The bottleneck is review capacity. Nine Claude packets are stacked behind INPUT-02 (INPUT-02 → F002 → F037 → F031 → F040 → F049 → F009 → F014 → F051), and every one of them was waiting on Codex. Meanwhile the Gemini lane was idle.
- **Reviewer routing.**
  - The Gemini lane may now review Claude and Codex branches. It never reviews fixes to code it originally wrote.
  - Codex keeps the INPUT-02 re-review, for continuity, and the MONITOR-OUTPUT-01 fix review.
  - Gemini reviews the stacked packets F002 through F051. Each review covers only that packet's changes on top of the previous branch in the stack. A short confirmation pass follows once the stack is updated onto integrated `main`.
  - The Claude lane takes the SAFE-MODE-01 follow-up review.
- **Reassignment.** F070 (CSV and manifest export) moves from Codex Wave 5 to the Gemini lane as EXPORT-CSV-01. It is engine-only; the UI stays with the Claude lane. The JSON manifest format stays with F030.
- **Gate lock.** Only one full gate (`scripts/build.ps1 -Test` or `docker build`) runs at a time. Holders set `gate_lock` in docs/WORK-QUEUE.yaml.

## Lane strengths, which decide every assignment

- **Claude Code lane (`claude/*`):** design and interaction work.
  - Native Qt/QML UI built with ui-design-skills, plus native design reviews.
  - Interaction contracts: focus, selection, targets and command precedence.
  - Accessibility.
  - Multi-component UX features that need judgment.
  - Task packets and specifications.
  - Independent review of Codex branches.
- **Codex lane (`codex/*`):** long autonomous runs that are terminal-heavy and test-driven.
  - C++ core and worker services, platform adapters and IPC.
  - Build, packaging, Linux, Docker and CI.
  - Instrumentation, benchmarks, soak and fuzz harnesses.
  - Parsers, codecs, export engines and file-operation engines.
  - Independent review of Claude branches.
- **Features with both a core and a UI side** split into a Codex engine packet and a Claude UI packet, joined by a contract the lead freezes. For example, Codex builds the batch-rename engine and Claude builds the preview UI.

## Concurrency protocol

1. **Naming.** Branch `claude/<task>` or `codex/<task>`, worktree `.worktrees/claude-<task>` or `.worktrees/codex-<task>`, build directory `build-<lane>-<task>`. Gemini keeps `agent/*`. No lane edits another lane's worktree.
2. **Lane limits.**
   - Each lane runs one active implementation task and one reviewer, and reviews always cross lanes: Claude reviews Codex, Codex reviews Claude, and Gemini work gets a Claude or Codex reviewer.
   - This replaces the earlier rule of at most two implementation agents and one reviewer under a single lead.
3. **Contracts first.**
   - Before a wave starts, the lead commits on `main`: the shared headers, the task packet in `docs/<TASK>-PLAN.md`, and the queue entry.
   - Lanes branch from that commit, and any change to a frozen header goes back through the lead.
4. **Hot-file rules,** to reduce conflicts with Gemini and between lanes:
   - **QML.** Only one lane edits `ui/Main.qml` per wave, and it is normally Claude. Codex tasks expose services or properties and ship no QML except what their packet names.
   - **Commands.** New commands go in a per-task block inside the controller's command table, with one block per task ID, so merges stay mechanical.
   - **Tests.** New UI tests go in new per-feature test files (`tests/<feature>_ui_tests.cpp`) instead of growing `tests/grid_ui_tests.cpp`. The lead adds the CMake targets in the contract commit.
5. **Serial integration.**
   - `docs/WORK-QUEUE.yaml` gains `integration_lock: {holder, branch, started}`.
   - The integrator does the following, in order:
     1. Set the lock.
     2. Merge `main` into the reviewed branch.
     3. Run `scripts/build.ps1 -Test` and `docker build -t glancelot-browser:main .`.
     4. Merge with `--no-ff`.
     5. Record the evidence.
     6. Clear the lock.
   - Nobody merges while the lock is held, and that includes Gemini.
   - A branch that conflicts after a merge goes back to its lane.
6. **Done** keeps the existing rule: independent review plus checks on the integrated commit. Hardware gates are reported as unverified unless they were measured on real hardware.

## Assignment of the full remaining inventory

Gates on Gemini-owned work are marked ⛔<task>. A packet starts only after that task is integrated.

### Wave 0: close-out (integrator)
- FOLDER-JUMP-01: done. Integrated `3e06e78`; Windows 11/11 (154.23s) and Linux 11/11 (63.86s) were measured on the integrated tree.
- CANVAS-UI and DECODE-01, still in `integration`: Codex writes the qualification scripts (part of PLATFORM-01). The tasks stay open until physical DPI and performance runs are recorded.

### Wave 1 (no Gemini gates)

| Lane | Packet | Scope |
| --- | --- | --- |
| Claude | **INPUT-02** + F055 | Complete scoped input matrix; text, modal and pane precedence; modifier-wheel rules; chord and hold lifecycle; focus-loss cancellation; context menus with command-route parity. |
| Codex | **PERF-MEASURE** | Bounded timing spans, fixture manifest, stage and result schema, baseline replay, and cache hit/miss distinction. |
| Codex (next) | **PLATFORM-01** + F080 + PERF-BUILD basics | Linux GUI package; Windows and Linux package evidence; reproducible build provenance; hardware qualification matrix scripts. |

### Wave 2

| Lane | Packet | Scope |
| --- | --- | --- |
| Claude | F002 | Breadcrumbs and path completion in the path bar. |
| Claude | F037 | Loupe, zoom presets, navigator overview and zoom lock (canvas UI). |
| Codex | F063 | File-change reconciliation: bounded watchers; replace, rename and delete; overflow recovery. |
| Codex | F064 | Safe mode, config diff and settings backup/restore. |
| Codex | F057 | Crash-recovery restore engine. The UI is a Claude follow-up. |

### Wave 3 (gated: ⛔F004, F010 and F011 for the directory model; ⛔META-HUD-01 for metadata)

| Lane | Packet | Scope |
| --- | --- | --- |
| Codex | PERF-ENUM + F016 | Incremental listing and publication, stable anchors and visible continuation. |
| Codex | PERF-THUMB | Size buckets by device pixel ratio, shared visible thumbnails and hidden-pane cancellation. |
| Codex | F042 core | Capture-date and dimension sorts through the metadata service. |
| Claude | F009 | Details/list mode. |
| Claude | F014 | Thumbnail badges. |
| Claude | F013 | Group headers. |
| Claude | F042 UI | Metadata filter chips. |

### Wave 4: culling and review (M4)

| Lane | Packet | Scope |
| --- | --- | --- |
| Codex | F025 engine | Sidecar and XMP conflict service. |
| Codex | F022 core | RAW+JPEG, sidecar and motion-pair asset grouping. |
| Codex | F028 core | Burst-grouping algorithm. |
| Claude | F025 UI | Pick, reject, stars and labels, with keyboard advance. |
| Claude | F029 | Culling progress. |
| Claude | F026 + F027 | 2-up and 4-up comparison with linked views; blink, wipe and difference. |
| Claude | F028 UI | Stack UI. |
| Claude | F012 + F030 | Basket, collections and manifests. Codex owns the manifest file format. |

### Wave 5: organization and handoff (needs ⛔F018, F020 and IO-01)

| Lane | Packet | Scope |
| --- | --- | --- |
| Codex | F019 engine | Batch rename: tokens and collision dry-run. |
| Codex | F017 + F062 engine | Operation journal, undo and restart reconciliation. |
| Codex | F021 | Platform drag-and-drop adapters. |
| Codex | F066 | Open-with argument arrays and tool slots. |
| Codex | F068 | Shell associations and single-instance IPC. |
| Codex | F069 | Command-line entry points. |
| Gemini | F070 (EXPORT-CSV-01) | CSV and path-list export engine. Reassigned from Codex on October 20, 2026; the UI is a later Claude packet. |
| Claude | F019 UI | Before/after preview. |
| Claude | F017 + F062 UI | Operation center and undo UI. |
| Claude | F061 | Recovery UX for unavailable shares, corrupt images and missing codecs. |
| Claude | F021 UX | Drop feedback and keyboard alternatives. |
| Claude | F066 settings UI | Tool-slot settings. |

### Wave 6: viewing and color

| Lane | Packet | Scope |
| --- | --- | --- |
| Codex | PERF-DECODE + F033 | Progressive preview tiers. |
| Codex | PERF-RENDER | Texture reuse and GPU-loss recovery. |
| Codex | F043 | Capability model. |
| Codex | F044 | Per-monitor ICC; physical evidence required. |
| Codex | F038 compute | Sampling and histogram computation. |
| Codex | F034 | Tiled viewing. |
| Codex | F047 | Lossless JPEG transforms. |
| Codex | F046 + F071 + F032 | Export engines. |
| Codex | F045 | AI-metadata parsers. |
| Claude | F038 UI | Sampler, guides and clipping overlays. |
| Claude | F031 | Slideshow. |
| Claude | F040 | Reference window. |
| Claude | F032 + F046 + F071 UI | Export preview and options. |

### Wave 7: workspace and accessibility (needs ⛔UI-SETTINGS-01)

| Lane | Packet | Scope |
| --- | --- | --- |
| Claude | F049 | Layout presets and focus mode. |
| Claude | F051 | Shortcut recorder and conflict explanations. |
| Claude | F052 | Assistive-technology support; NVDA and Orca trials are unverified unless actually run. |
| Claude | F053 | High contrast and touch targets. |
| Claude | F054 | Localization and RTL. |
| Claude | F015 | Tabs and sessions. |
| Claude | **MONITOR-CUSTOM-01** | Needs ⛔MONITOR-OUTPUT-01. |
| Codex | PERF-RESOURCE | Shared accounting across the delivered cache and scheduler. |
| Codex | PERF-POWER + F059 | Power and performance profiles. |
| Codex | F058 | Diagnostics backend. Claude does the panel UI. |
| Codex | F060 | Cache controls. |

### Wave 8: remote, container and hardening (M6)

| Lane | Packet | Scope |
| --- | --- | --- |
| Codex | PERF-CONTAINER + F079 | Container profiles and deployment self-check. |
| Codex | PERF-REMOTE + F073, F074, F076 and F078 | Remote quality and responsiveness, resize, clipboard bridge and access modes. |
| Codex | PERF-WINDOWS | Windows platform qualification matrix. |
| Codex | PERF-LINUX | Linux platform qualification matrix. |
| Codex | PERF-SOAK | Soak and fuzz harnesses. |
| Codex | **MONITOR-VERIFY-01** | Codex builds the harness. The reviewer must be from a different lane, and physical hardware is unverified unless measured. |
| Claude | F075 | Server-path and read-only indicators. |
| Claude | F072 | GPS map handoff. |
| Claude | F073 UI | Remote quality and connection-status UI. |
| Claude | — | Final UX retest of the five workflows in FEATURE-OPPORTUNITIES §14. |

### Conditional or specialist work (separate feasibility packets first)

| Item | Owner and gate |
| --- | --- |
| F005, F006 | Codex engine, Claude UI. Needs ⛔IO-01 and FILTER-01. |
| F023, F024 | Codex. Review-only results, with no automatic deletion. |
| F039 | Codex core, Claude reading UI (E1 extension). |
| F048 | Codex (E3 extension). |
| F077 | Codex, behind an authentication design gate. |
