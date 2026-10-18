# PANES-01 layout contract and delivery

Lead freeze, October 18, 2026. Assigned branch `agent/panes`, worktree `.worktrees/panes`, build `build-panes`. Native Qt/QML, existing GPU ImageCanvas, semantic Theme tokens. This packet implements the full approved pane scope; detached multi-display windows remain MONITOR-CUSTOM-01.

## Stable layout data

Pane IDs: tree, preview, grid, filmstrip, topbar, statusbar. Future metadata uses the same registration contract. A pane stores a dock region (left/right/top/bottom/center), order within that region, preferred extent in logical pixels, visible preference and frozen flag. Preview/grid center split stores orientation and normalized ratio. Global layout lock forbids all geometry/placement mutations. Freeze prevents position/size changes and auto-hide while content remains interactive. Visibility changes for frozen panes require an explicit unlock; freezing is distinct from tree pinning. View modes still determine whether grid/preview content participates; combined visibility is GRID-PREVIEW-01 using this same center contract.

A QObject layout model owns validated state, emits change notifications and exposes move, resize, freeze, global-lock and reset commands. Never mutate QML anchors directly from transient pointer handlers as the persistence source. Drag begins from a labeled handle; preview legal drop zones, allow Escape cancellation, commit once on release. Resizing clamps to minimums; lock changes during a gesture cancel it. Keyboard actions provide equivalent region/order/extent changes and move focus to the moved pane. A layout reset is always reachable through Settings/command search; global lock and frozen states are visible and announced.

## Persistence and concurrent windows

Store versioned layout under a dedicated settings object keyed by window slot; slot0 is session-only. Preserve all unrelated settings and other slots through the existing locked atomic writer. Update only fields explicitly changed, merging under the writer lock; do not overwrite another window's pane changes from an old full snapshot. Persist explicit gesture completion and keyboard commands, never responsive reflow or startup constraints. Validate IDs, enum values, finite numeric ranges and size bounds; ignore unknown future pane IDs while retaining their stored data. Invalid known fields fall back individually; corrupt layout falls back to reachable defaults with feedback. Reset updates only this slot's layout.

## Adaptive native composition

Use region hosts and native Qt Quick layout/split controls around the unchanged canvas. At compact sizes, constrain displayed extents and allow reachable overlay regions without rewriting preferred saved geometry. A move to a region that would leave no usable content is rejected with a reason. Tree address/copy/error rows stay together. Filmstrip supports horizontal and vertical presentation by region; top/status controls reflow when moved into vertical regions. Minimum pane sizes, large text and focus traversal are explicit. Do not decode or scan from layout handlers; hide/detach inactive models and preserve immutable source-generation semantics. Canvas resize retains fit behavior and physical zoom rules.

## Delivery and verification

1. Layout model and validated per-slot persistence, generated contract tests (unknown/invalid fields, concurrent merges, lock/freeze/no-op/reset and restart).
2. Region hosts, resize/move handles and pointer/keyboard operations for all six primary pane IDs; preserve image/grid focus and content state.
3. Settings controls and command discovery, reset/recovery, responsive composition and state announcements.
4. Independent source and native UI design review; integrated Windows script/test gate plus Linux functional checks. Exercise every pane in every legal region, frozen/global lock, drag cancellation, narrow/wide, theme modes, large labels, slot0/session and persistent slots, failed writes and concurrent windows. Physical mixed-DPI/monitor behavior remains unverified without actual hardware; virtual tests prove only their declared scope.

No task completion until the full pane acceptance is demonstrated. Contract foundation alone is an intermediate milestone.
