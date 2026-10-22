# Lead closeout and remaining lane work, October 22, 2026

This packet records the remaining work after MONITOR-CUSTOM-01 integration. It does not approve the unsynchronized Claude stack or claim physical monitor qualification.

## Claude stack: current-main correction contract

The independent original Gemini approvals remain useful, but the synchronized pins in `CLAUDE-STACK-SYNC-2026-10-22.md` use main5424cef and predate integrated monitor customization. Claude retains implementation ownership, existing `claude/f002` through `claude/f051` worktrees and assigned `build-claude-*` directories. Synchronize serially from the final main of this round, propagating each predecessor forward. Never discard current main's monitor, SAFE, recovery, command routing or deferred persistence changes. Return exact predecessor/tip pins and actual targeted output for Gemini's required confirmation pass. No integration until those confirmations and full locked gates pass.

For this serial correction wave, Claude owns `ui/Main.qml` in its assigned stack worktrees. Codex monitor-verification harness work must use feature tests and harness files rather than editing Main.qml. Root CMake changes remain lead-owned and are reviewed with the synchronized packet.

The lead freezes these resolutions for that synchronization:

- Shared `PaneLayout` and `ViewerController` headers retain current main's API. Add the stack's APIs alongside it; do not replace the canonical monitor pane freeze/lock authority or introduce a second live owner. F049 presets must obey the current canonical lock and frozen-pane rules, maintain per-slot isolation for placement, and preserve unknown/newer preference data. Named controller layouts and monitor workspace presets must have distinct names and scopes in the UI.
- F040 reference windows and MONITOR-OUTPUT pinned output may coexist as explicitly distinct commands. Both participate in existing aggregate pixel/texture admission and release accounting. They must not cause duplicate decoding, bypass budgets, or change the selected image. Add parity tests for close, pin replacement and recovery while both are active.
- F015 tab persistence must use the production recovery session, not a second settings writer. Extend `recovery::CheckpointInput` and `ResumeState` with optional value-owned tab state, appending defaulted fields so existing call sites retain their meaning. Tab state contains at most eight folder/image records and an active index. Persist it atomically with this slot's folder/image and dirty checkpoint revision under the existing claim and monotonic sequence. The latest pending immutable checkpoint includes the tab snapshot. Read legacy entries without tabs as a single current location; validate bounds and active index; preserve unrelated slots and unknown fields. Do not restore navigation automatically under SAFE or reset modes. Do not mark the session clean until the newest complete snapshot has persisted. Add restart, old-entry, malformed/oversized state, stale sequence, busy/failure/retry, other-slot preservation, and SAFE/reset tests. Scope is the recovery checkpoint/session/controller files and dedicated feature tests; no relaxation of the existing identity or clean-marker gates. This freezes the extension's behavior; submit the exact header diff with the correction for independent review.
- F054 must cover user-facing strings added by current main in MetadataHud, AnimationBar, RenameFlow, TransferHud and presentation/settings controls. Preserve interpolation and accessibility names; include pseudo-locale, clipping and RTL evidence. UI work follows AGENTS.md's native Qt design skills.

The stack remains changes required until that work and current-main confirmation are supplied. The existing original approvals do not approve these new changes. Gemini reviews only the Claude stack; it must not review its own monitor implementation.

## Monitor qualification

MONITOR-VERIFY-01 is dependency-unblocked. Codex owns the harness and evidence packet; an eligible different lane must independently review it. The completed monitor customization evidence already proves generated-media software behavior and real native two-screen frame/move behavior on the recorded Intel Iris Xe / same-DPR Windows topology. Reuse that evidence with its exact pins; do not rerun or relabel it as a physical display-accuracy result.

Still required by SECOND-MONITOR-PLAN.md: actual 100/125/150/175/200% and mixed-DPI moves, differing refresh, physical 1:1 patterns and qualified per-monitor ICC; physical hotplug, suspend/resume, GPU-loss/recovery and sustained resource measurements; separate native Linux Wayland and X11 trials and relevant assistive-technology checks. HDR requires calibrated evidence. The Docker/noVNC output path remains unsupported for multi-output until separately qualified. No headless result or screenshot establishes these physical properties.

CANVAS-UI and DECODE-01 retain their recorded physical qualification limits. They are not new source-code blockers for the already integrated monitor packets. No unrelated inventory feature is promoted by this closeout.
