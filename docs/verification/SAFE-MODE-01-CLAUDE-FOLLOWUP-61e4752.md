# SAFE-MODE-01 follow-up review of 61e4752 (Claude lane)

**Pinned:** `git diff 7bffd68 61e4752` (src: pane_layout.cpp, settings_file.cpp, settings_retry.hpp, viewer_controller.cpp; tests/recovery_controller_tests.cpp; scripts/verify-recovery.py; evidence docs). Read-only source review; I built and ran nothing.

## Verdict
**Source: favourable. No blocking defect found.** Do not mark done until the full Windows and Docker gates run on the integrated commit; the packet's own README states those were cancelled while queued behind another holder (`obsolete-gates-cancelled.txt`, and `windows-full.txt` / `linux-docker-full.txt` contain only lock-wait lines). Only the three focused recovery tests (137, 61 and 23 cases, all passing) are actual output.

## Checked
- **Confirmed-binding rollback:** the rollback value is re-read from the persisted map inside the writer edit, so a superseded earlier attempt can no longer leave an unconfirmed binding as the rollback target. The conflict callback restores the live binding only when no newer local value replaced it. Owner deletion is guarded before the second signal.
- **`skipUnchanged`:** applies only to the private retry paths. It returns `NoChange` only when the edit leaves the base object equal and the primary is not a recovered one. Ordinary `updateSettings` and recovered-primary repair are unchanged, and `retryNoChangeStillRepairsRecoveredPrimary` covers both.
- **Layout overlay:** restricted to the owner's own queued edits and to the same configuration directory. Completion declines to touch feedback when the slot, directory or feedback text changed.
- **Sort:** one stable `sort` channel carrying all unsaved changed fields; cleared only on success. A 200-change burst fits the queue cap and keeps foreign fields.
- **Begin retry:** a successful retry replaces only the exact obsolete "unavailable" status text.

## Low-severity observations (not blocking)
1. A deferred layout save that fails after the user left that slot, or after feedback changed, only logs a warning. The user is never told, and returning to the slot reloads the old persisted values.
2. After a successful binding save, foreign keys merged into the live map are not added to `confirmedBindings`. The rollback is re-read from the file when the edit runs, so this only matters when the saved map is invalid.
3. Earlier findings 3 (owned-position restore vs a live Session sequence), 4 (exit retry without backoff) and 5 (preimage fsync) are untouched by this delta and remain recorded, not fixed. Finding 6 (`-qmljsdebugger`) is now explicitly handled in the parser, with an error requiring an inline operand.

## Not verified
Native test execution, Docker, physical power loss, and GPU/DPI behaviour. I did not re-run the focused tests.
