# Agent-led execution

The product baseline is BUILD-PLAN.md and the 88 rows in REQUIREMENTS.md (87 original requirements plus approved UX-12). This document records implementation authority from the user's accepted agent-led plan and subsequent roadmap amendments.

Four slots: lead/integrator; two scoped implementation agents; independent reviewer. Each implementation uses a branch/worktree/build directory. The lead alone changes contracts, shared build configuration and WORK-QUEUE.yaml, and integrates sequentially. Reviewer must not approve their own implementation. All async work has generation/revision validation and memory admission.

Task packet: ID/requirements; outcome; dependencies/base commit; owned files/exclusions; interfaces; acceptance/failure/cancellation/platform cases; verification commands; handoff commit/evidence/limitations. Lifecycle ready -> implementing -> review -> integration -> done. Two failed repairs trigger narrowed investigation, not weakened acceptance.

Waves: (0) toolchain/contracts/fixtures; (1) isolated decode + GPU/SDR/DPI proof; (2) daily viewer alpha; (3) browsing/HUD/RAW; (4) culling/XMP/safe file operations; (5) qualified formats/diagnostics; (6) hardening/release; (7) archives/motion/HDR expansions. Each wave ends with a runnable artifact and evidence. Linux hardware claims remain unverified until measured; WSL/VM only validates corresponding capabilities.

## Approved next milestone: M2.1 folder tree and window resume

The user confirmed this reprioritization on October 17, 2026. Finish integration/review of the already-started filmstrip, then prioritize the collapsible folder tree before starting additional M3 feature work. The tree must not wait for favorites, a full grid, RAW or the other HUDs. Use relevant ui-design-skills for the Qt/QML tree and its independent UI review.

Execution order: TREE-01 (lazy collapsible folder panel), WINDOW-SLOTS-01 (stable numbered window ownership and coordinated storage), then WINDOW-RESUME-01 (save and restore each slot's last folder/image). TREE-01 and slot/storage foundations may run in the two implementation lanes after the lead freezes their interfaces; restoration depends on those slot contracts. Only the lead edits shared contracts and integrates.

Slot semantics: first window opened is slot 1, second is slot 2, etc. Keep assignments fixed despite close order; keep saved locations for closed and unopened slots. After full application exit, reset the opening-order counter and restore each slot only when its corresponding window is opened. Do not spawn all remembered windows automatically. Explicit file arguments override restoration for that window; failed/missing-path restore cannot overwrite another slot or erase the saved location.

M2.1 exit requires independent tests for keyboard/tree behavior, dark/light/compact layouts, two or more distinct window positions across complete exit/restart, reversed close order, unopened saved slots, simultaneous launches, missing sources, explicit shell-open overrides and atomic preference writes preserving themes/bindings. The roadmap addition is not evidence that either feature is already implemented.

Author tests -> independent review/acceptance -> serial integration -> integrated checks -> done. Your feedback is advisory and not a routine gate. Agents continue ready tasks, preserving evidence of infrastructure blocks. Public publishing, paid services and new external access are separate actions. Forecast from first ten integrated tasks, not agent-count multiplication. Sessions checkpoint work; agents do not promise execution after the session ends.

## Current continuation: M3 browsing

M2.1 is implemented and independently verified on Windows; see WORK-QUEUE.yaml and verification/WINDOW-SESSIONS.md. Physical-display qualification and Linux GUI delivery remain open gates, not inferred from Windows or WSL results.

Next scoped task: GRID-01 adds a virtualized current-folder grid using the existing bounded thumbnail service and folder snapshot. Selection stays separate from opening a photo. Keyboard/grid/modal routing, hidden-view cancellation, 10,000-row delegate bounds, compact/wide layout and theme checks precede independent review and serial integration. No new decoder formats or media mutation are part of this task. Remaining M3 work still includes sorts/filters, favorites, other HUDs, input matrix/chords, raster/RAW qualification, caching/lookahead, and performance/storage tuning.

## Initial contracts and allocation

Frozen headers in include/glancelot establish identity/revisions, commands/navigation, decode/pixel metadata and viewport/display capabilities. Initial source support is JPEG/PNG only; the capability page must report actual delivery instead of the full target inventory.

- Lead: build/dependency/bootstrap scripts, decoder process, ViewerController backend, app entry, documentation, integration.
- Navigation agent: src/core/navigation.cpp and tests/navigation_tests.cpp.
- Canvas agent: src/render/* and ui/Main.qml.
- Independent reviewer: tests/acceptance_tests.cpp and docs/verification/ACCEPTANCE.md, then independent code/behavior review.
