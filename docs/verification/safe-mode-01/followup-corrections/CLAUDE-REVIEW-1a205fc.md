# SAFE-MODE-01 Claude-lane fix review of 1a205fc (delta 7bffd68..1a205fc)

I used Read/Grep/Glob only. I ran no commands, edited nothing, and did not write a plan file. I read `.cache/second-review-fix-delta.patch` and the current source in `settings_file.cpp`, `pane_layout.cpp`, `viewer_controller.cpp`, `recovery_session.cpp` and `settings_transaction.hpp`. I did not read the new test bodies in the working tree or any generated logs, only the test hunks in the patch.

**Disposition: conditionally favourable, not approved for integration.** My earlier F1–F4 are largely fixed. I found one new defect in the F1 rollback that I think should be fixed, and five Low items to fix or record. I make no done claim. Fresh full gates and a further pinned review are still required.

## Ranked findings

### N1 (Medium-Low): the F1 rollback restores an unsaved optimistic value after a superseded edit
- **Where:** `viewer_controller.cpp:1453` (`before` snapshot), `:1455` (channel `"bindings/"+id`), `:1469` (rollback). Supersession is at `settings_file.cpp:276-277`.
- **Trigger:** While a settings write is Busy, the user assigns shortcut X to `id`, then immediately assigns Y. The second submission has the same channel, so the queue discards the first edit and its completion.
- **Why it goes wrong:**
  - The second call's `before` is captured after the first optimistic assignment at `:1483`, so `before[id]` is X.
  - X was never validated or saved, because its edit was discarded.
  - If a foreign window then takes an overlapping Y, the rollback at `:1469` sets `d->bindings[id]=X`.
- **Impact:** The live window keeps X, which was never persisted or conflict-checked. Disk still holds the original value A. The status says "Clear it first". The local overlap check at `:1451` now treats X as authoritative.
- **Fix:** Roll back to the last persisted or confirmed binding, not the call-time snapshot. For example, keep a per-id `confirmedBinding` in `Private`, or carry the oldest pending call's `before` forward when superseding. Emit `bindingsChanged` after the rollback, which the code already does.
- **Missing evidence:** Two back-to-back `setBinding` calls under a held lock, then a foreign conflict, asserting `bindingFor`.
- **Confirmed good:**
  - A single-edit conflict now rolls back (`:1468-1471`).
  - The test asserts the live binding and exact primary and `.bak` bytes (patch lines 236-239).
  - The no-change path (`settings_file.cpp:225`) returns the default `Edit`, whose decision is `NoChange` (`settings_transaction.hpp:24`). `transact` then calls `finish(NoChange)` before any evidence or backup write (`:178-181`), and `updateAt` maps it to Ok. So F1's "no unnecessary primary/backup rewrite" is correct.

### N2 (Low): layout save-failure feedback depends on string equality
- **Where:** `pane_layout.cpp:120` (`feedback_!="Saving layout preferences…"`).
- **Trigger:** A deferred layout write fails after Busy exhaustion, but the user has since hit an "Unlock…" or "center reserved" message, or has changed slot or namespace.
- **Impact:** The completion returns early, so the failure is never reported. It is silent, which is a different outcome from the old stale-overwrite bug. Using a display string as an ownership token is also fragile. If the text changes, the guard silently stops working.
- **Fix:** Use a feedback owner or sequence token (a `savingToken_` integer), or record a "last deferred save failed" flag that load can surface.
- **Confirmed good:**
  - Slot, namespace and feedback guards fix F2's clobbering. They need `completion->deferred`, so the immediate path is unaffected.
  - The pending-later-write check at `:121` uses the slot prefix. It runs after `pop_front`, so it excludes the completing edit. Success keeps "Saving…" until the last pending slot edit finishes.
  - The three new tests would fail without these guards. The completions run synchronously inside `run()`, so a disk-poll assertion cannot pass before the clobber would have happened.

### N3 (Low): pending layout channels can exceed the 128 cap across many slots
- **Where:** the channel format at `pane_layout.cpp:78` includes the slot, pane id and field set.
- **Trigger:** Many slots each hold queued layout edits, together with the unique reset and lock channels. The count can pass 128 while Busy lasts.
- **Impact:** The overflowing edit is rejected with a synchronous Busy completion. `persist` ignores the return value, and `completion->deferred` is false in that case, so the failure feedback does run. The session keeps the change but the save is lost. This is bounded and fail-visible, and it is also pre-existing.
- **Fix:** Record this as a known limit, or coalesce by slot.

### N4 (Low): the sort sequence guard is effectively dead code, and its test is thin
- **Where:** `viewer_controller.cpp:1034`.
- **Why:**
  - Completions run synchronously inside `run()` or `submit`. Superseded completions are discarded by the queue, so `sequence!=preferenceSequence` can only fire if a callback re-enters `changeSort`.
  - `preferenceSequence` is now used only by sort (grep).
  - It is harmless but does not protect what the README says it does.
- **Merge behaviour:**
  - A stable channel `"sort"` with accumulated `pendingSortFields` is correct for key, direction and seed. Revert-to-previous fields are re-sent with the latest value.
  - Leftover fields from a non-Busy failure (such as Corrupt) stay until a later success. They are bounded to three keys.
  - A later success may rewrite those older values over a foreign window's newer sort. This is acceptable, but it should be disclosed.
- **Test gaps:**
  - `sortBurstCoalescesWithoutLosingChangedFields` uses one channel, so "within the queue cap" is trivially true.
  - It has no loaded directory, so `startResort` is not exercised.
  - It has no failure case, so retention of `pendingSortFields` and `sortSaved` is unverified.
- **Fix:** Add a failure-path test, or drop the guard and the README claim.

### N5 (Low): the successful begin retry overwrites a checkpoint-failure status
- **Where:** `viewer_controller.cpp:270-273`.
- **Trigger:** Begin fails, then a checkpoint failure sets the "could not be saved" status. A later begin retry succeeds.
- **Impact:** The status becomes "Browsing recovery is available." while that sequence may still be unpersisted. `reportedCheckpointFailure` was already set at `:287`, so the failure is not re-reported. This is stale feedback, not data loss, because the next checkpoint will try again.
- **Fix:** Replace only the "unavailable" string, or clear `reportedCheckpointFailure` when begin recovers.
- **Confirmed good:**
  - A retry only happens from `Session::accept` through `start` (`recovery_session.cpp:107-118`).
  - Success resets `beginFailureReported`, refreshes `recoveryResume` and re-triggers `restoreWindow` only when appropriate.
  - A repeated begin failure does not re-report, because `beginFailureReported` stays latched.
  - Replacement is restricted to the two exact strings and is epoch-guarded.

### N6 (Low): no-op skipping changes backup freshness
- **Where:** `settings_file.cpp:225`, applied by `run`, `submit` and teardown.
- **Behaviour:**
  - An unchanged retry-path edit no longer refreshes `.bak`. This is intended, and it is gated by `!view.recovered` so recovered-primary repair still writes.
  - A legacy primary without `"schema"` is never normalised by a no-op edit. This is harmless.
  - `updateSettings` and `d->save` callers are unchanged.
- **Test gap:** Only the binding path asserts byte equality. No test shows that a no-op layout or sort edit leaves the files untouched, or that a recovered-primary repair still writes.

### N7 (Low, evidence): the reentrant owner-deletion test
- **Where:** `deferredBindingNotificationMayDeleteOwner`, and `viewer_controller.cpp:1478-1479`.
- **Confirmed good:**
  - Deletion inside `bindingsChanged` is handled. `alive` is created before the emit. Nothing after the emit touches `this` unless `alive` is set.
  - The completion lambda is a stack-local `value` in `run()`, and `QPointer<SettingsRetries>` handles the retries child being destroyed.
  - The test connects after `setBinding` returns, so it targets the deferred emit, and it checks that the binding was persisted.
- **Limit:** There is no sanitizer evidence. Deleting a sender while it is emitting is tolerated by Qt but not recommended, so ASan or a UBSan run would be valuable.

## Requested audit items

- **F1 rollback and no rewrite:** Correct for a single edit (see N1 for the superseded case).
- **F2 overlay:**
  - The overlay is namespace-filtered, uses a canonical directory and replays edits in queue order. Supersede-to-tail preserves ordering.
  - It does not leak across roots. It runs on the owner thread only, and only `PaneLayout` passes itself as the owner.
  - The one shared mutable it can touch is the edit's `unsupported` flag, which is benign.
  - The test did not assert "Saving…" feedback after returning to the slot.
- **F3:**
  - Teardown now warns when budget remains unused and edits are left (`settings_file.cpp:265`). Edits are still dropped without the owner's completion, which is documented in the header.
  - Stable sort channel: see N4.
- **F4:** See N5. The future-record-then-repair test is a good regression for the retry path.
- **Bounded and value-owned:**
  - All pending edits capture values only. `pending`, `pendingSortFields` and layout panes are small, and the queue cap is 128.
  - Foreign and unknown values are preserved, because edits are field-level and the sort test checks `future` and `seed`.
- **Read-overlay reuse and no-op optimisation:** No functional regression found beyond N2, N3 and N6.

## Evidence limits
- I did not execute anything. The 137, 61 and 23 case results and the three-test CTest run are as reported by the parent.
- Full Windows and Docker gates on 1a205fc have no result. The 37/37 run on older source is not current qualification.
- Power-loss durability, physical GPU/DPI behaviour, and Qt internals (`QSaveFile` sync, `-qmljsdebugger` consumption) remain unverified.
- Non-UTF-8 paths and cancellation under process-level execution are still not covered.
- I did not re-derive the registry or layout hashes. The `verify-recovery.py` change is a message-only edit that adds fields from the existing report, with assertions unchanged.

## Recommendation
1. Fix N1, or have the lead waive it with a recorded limit. Add the two-call supersede test.
2. Fix or record N2 through N6. N2 and N4 are the cheapest to fix.
3. Run the fresh full gates under the gate-lock. Then send the fixes back for a further pinned review.
