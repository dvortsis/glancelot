# F040 contract and delivery: reference window

Frozen by the lead on October 19, 2026. Lane Claude Code; branch `claude/f040` (stacked on `claude/f031`); build `build-claude-f040`; reviewer from the Codex lane.

## Frozen interface (approved additions)
- `ViewerController`: properties `referenceImage` and `referenceName`, invokables `pinReference()` and `clearReference()`, signal `referenceChanged`. The reference shares the decoded pixels (implicit sharing), so pinning costs no decode and no extra pixel memory.
- Registry: `reference-pin` (scope `canvas`) and `reference-close` (global), both unbound.

## Behaviour
- Pinning opens a separate window showing that photo with its own view: fit on open, wheel zoom, drag pan, `F` fit, `1` actual size, double-click refit. Browsing in the main window never moves it. Pinning again replaces it; there is at most one.
- Closing the window, Escape inside it, or `reference-close` unpins. Pinning with no photo shows a notice and opens nothing.
- It is read-only: nothing is written to the original media.

## Not done
Linked zoom and pan with the main view (that is the F026 comparison work), restoring the reference after restart, and multi-monitor placement.
