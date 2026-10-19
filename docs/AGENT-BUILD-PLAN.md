# Agent-led execution

The product baseline is BUILD-PLAN.md and the 96 rows in REQUIREMENTS.md (87 original requirements, approved UX-12, and eight workspace/file-manager additions). This document records implementation authority from the user's accepted agent-led plan and subsequent roadmap amendments.

Four slots: lead/integrator; two scoped implementation agents; independent reviewer. Each implementation uses a branch/worktree/build directory. The lead alone changes contracts, shared build configuration and WORK-QUEUE.yaml, and integrates sequentially. Reviewer must not approve their own implementation. All async work has generation/revision validation and memory admission.

Task packet: ID/requirements; outcome; dependencies/base commit; owned files/exclusions; interfaces; acceptance/failure/cancellation/platform cases; verification commands; handoff commit/evidence/limitations. Lifecycle ready -> implementing -> review -> integration -> done. Two failed repairs trigger narrowed investigation, not weakened acceptance.

Waves: (0) toolchain/contracts/fixtures; (1) isolated decode + GPU/SDR/DPI proof; (2) daily viewer alpha; (3) browsing/HUD/RAW; (4) culling/XMP/safe file operations; (5) qualified formats/diagnostics; (6) hardening/release; (7) archives/motion/HDR expansions. Each wave ends with a runnable artifact and evidence. Linux hardware claims remain unverified until measured; WSL/VM only validates corresponding capabilities.

## Approved M2.1 sequence and preserved acceptance gates

The user confirmed this reprioritization on October 17, 2026. The filmstrip, tree and window-session slices are now implemented on Windows; the following records the approved order and acceptance contract. The tree must not wait for favorites, a full grid, RAW or the other HUDs. Use relevant ui-design-skills for the Qt/QML tree and its independent UI review.

Execution order: TREE-01 (lazy collapsible folder panel), WINDOW-SLOTS-01 (stable numbered window ownership and coordinated storage), then WINDOW-RESUME-01 (save and restore each slot's last folder/image). TREE-01 and slot/storage foundations may run in the two implementation lanes after the lead freezes their interfaces; restoration depends on those slot contracts. Only the lead edits shared contracts and integrates.

Slot semantics: first window opened is slot 1, second is slot 2, etc. Keep assignments fixed despite close order; keep saved locations for closed and unopened slots. After full application exit, reset the opening-order counter and restore each slot only when its corresponding window is opened. Do not spawn all remembered windows automatically. Explicit file arguments override restoration for that window; failed/missing-path restore cannot overwrite another slot or erase the saved location.

M2.1 exit requires independent tests for keyboard/tree behavior, dark/light/compact layouts, two or more distinct window positions across complete exit/restart, reversed close order, unopened saved slots, simultaneous launches, missing sources, explicit shell-open overrides and atomic preference writes preserving themes/bindings. Implementation evidence is linked from WORK-QUEUE.yaml; the roadmap amendment alone is not completion evidence.

Author tests -> independent review/acceptance -> serial integration -> integrated checks -> done. Your feedback is advisory and not a routine gate. Agents continue ready tasks, preserving evidence of infrastructure blocks. Public publishing, paid services and new external access are separate actions. Forecast from first ten integrated tasks, not agent-count multiplication. Sessions checkpoint work; agents do not promise execution after the session ends.

## Agent lanes (October 18, 2026)

Claude Code, Codex and Gemini now work concurrently in separate lanes. [AGENT-LANES-PLAN.md](AGENT-LANES-PLAN.md) covers three things:
- which lane owns each remaining task, feature opportunity and performance packet;
- the per-lane limits and cross-lane review rule;
- the hot-file rules and the serial `integration_lock` protocol.

Gemini-lane tasks follow the Gemini plan and are not reassigned. The Codex brief for Wave 1 is [handoff/CODEX-WAVE1.md](handoff/CODEX-WAVE1.md).

## Current continuation: M3 workspace and file-manager priority

Use [FEATURE-OPPORTUNITIES.md](FEATURE-OPPORTUNITIES.md) for the broader candidate inventory and [PERFORMANCE-STRATEGY.md](PERFORMANCE-STRATEGY.md) for platform-specific implementation packets. They are planning references: preserve the eight approved priorities, map enhancements onto existing requirements/tasks, and promote new scope explicitly rather than treating every proposal as ready work. Performance tuning begins with reproducible measurement and retains all source-safety and platform-evidence gates.

The October 17, 2026 user amendment puts PATH-BAR-01, PANES-01, GRID-PREVIEW-01, FILE-GRID-01, DELETE-01, DELETE-KEYS-01, UI-SETTINGS-01 and TOPBAR-THEME-01 first, in that order subject to their dependencies. All are planned, ready tasks; this planning change authorizes no completion claim. Use the live queue for acceptance, contracts and verification. Pane freeze locks layout rather than content; deletion uses explicit target snapshots and Trash/Recycle Bin with no silent permanent fallback. Only this explicit deletion slice advances from M4. Assign worktrees/branches/build directories before implementation, freeze shared contracts with the lead, and include relevant ui-design-skills plus native design-review in UI task packets.

M2.1 is implemented and independently verified on Windows; see WORK-QUEUE.yaml and verification/WINDOW-SESSIONS.md. Physical-display qualification and Linux GUI delivery remain open gates, not inferred from Windows or WSL results.

GRID-01 and SORT-01 are implemented and independently reviewed on Windows. REVIEW-ALL-01 corrected persistence, scheduling, reveal, grid-retry and concurrent-sort defects and closed the previously missing independent grid rerun. See verification/CLAUDE-AUDIT.md for integrated checks and remaining platform limits.

After audit closure, execute the ready tasks recorded in WORK-QUEUE.yaml. After the priority workspace/file-manager tasks, continue PLATFORM-01 qualification and CACHE-01 / IO-01 resource foundations, followed by favorites, filters, metadata HUDs, input contexts, and qualified format expansion. A task marked done certifies only its declared slice/platform; it never closes every requirement ID it references. In particular PERF-04/PERF-05, full IN-02, and SYS-02 still have substantial unfinished acceptance work.

Use generated media, source hashes and actual command output. UI semantics may be tested with offscreen/software Qt Quick; GPU, physical displays and native file-dialog timing require separate evidence. Run the required Windows integrated build/test/package and core Linux checks where available. Do not claim Linux Qt delivery from WSL core-only tests.

## Initial contracts and allocation

Frozen headers in include/glancelot establish identity/revisions, commands/navigation, decode/pixel metadata and viewport/display capabilities. Initial source support is JPEG/PNG only; the capability page must report actual delivery instead of the full target inventory.

- Lead: build/dependency/bootstrap scripts, decoder process, ViewerController backend, app entry, documentation, integration.
- Navigation agent: src/core/navigation.cpp and tests/navigation_tests.cpp.
- Canvas agent: src/render/* and ui/Main.qml.
- Independent reviewer: tests/acceptance_tests.cpp and docs/verification/ACCEPTANCE.md, then independent code/behavior review.
