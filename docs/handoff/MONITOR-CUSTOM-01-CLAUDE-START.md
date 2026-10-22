# Claude start handoff: MONITOR-CUSTOM-01

The lead-owned contract blocker is removed. Implement the complete [frozen packet](../MONITOR-CUSTOM-01-PLAN.md) using Claude Code Sonnet5.5 in:

- Branch: `claude/monitor-custom-01`
- Worktree: `C:\src\Github\Glancelot\.worktrees\claude-monitor-custom-01`
- Build: `build-claude-monitor-custom-01`, inside that worktree
- Public coordinator contract: `include/glancelot/monitor_customization.hpp`
- Dock snapshot contract: the two new non-QML declarations in `include/glancelot/pane_layout.hpp`
- Lead-owned production/test registration: `cmake/MonitorCustomization.cmake`
- Independent reviewer: Codex; do not self-approve or use Gemini to approve changes to original Gemini monitor code

MONITOR-OUTPUT-01 and UI-SETTINGS-01 are already integrated. Start from the assigned worktree's frozen main, not the old monitor-output-fix or INPUT snapshot. The feature owns ui/Main.qml for this wave. Keep only one Claude implementation active: the currently building F002 work must finish or be deliberately put aside before implementation starts here. Do not run parallel hot-file edits. This serialization rule does not require the entire eight-packet stack to finish first.

Deliver the display picker and recovery commands, presentation appearance and viewport synchronization, all four real detachable/re-dockable panes, freeze/global lock behavior, named presentation/workspace presets, bounded per-slot atomic persistence and complete functional tests. Read the packet before coding: it freezes option keys, method semantics, settings schema/limits, command IDs, rollback, missing-display behavior, resource bounds and acceptance. No placeholder pane or silently deferred behavior counts as complete.

Read application-interface-design and all applicable UI foundation skills plus design-review; apply them to native Qt/QML with existing branding, Theme tokens and Ui* primitives. Independent native UI design review remains required. Use the new feature test files, not grid_ui_tests.cpp.

Do not edit docs/WORK-QUEUE.yaml, root CMake, lead-owned fragments or public interfaces. If implementation exposes a concrete interface gap, send the lead the exact requested amendment; no need to ask again for general implementation permission. Report started status to the lead so the queue can move ready→implementing.

Provide an exact candidate commit and actual targeted/full-gate output in docs/verification/MONITOR-CUSTOM-01-CLAUDE-HANDOFF.md. Both full gates use the main checkout's gate-lock wrapper exactly as shown in the packet. Hardware-only evidence remains explicitly unverified until MONITOR-VERIFY; functional logic and UI acceptance are mandatory now. Lead removes the temporary source-presence guard before final integration, checks BUILD_TESTING=OFF production wiring and performs serial integration after independent approval.

The freeze adds declarations and conditional build registration only; it does not implement or qualify this feature. Contract validation output is retained separately under docs/verification/monitor-custom-01/contract-freeze-2026-10-21.
