**Disposition: the correction is sound and I found no blocking regression.** The `window_slots` fix holds up on my read, but this is not an integration approval. Full Windows/Docker for this pin is still pending, and the `fs_watch` failure is still unexplained.

I used only Read/Grep/Glob. I ran nothing and didn't open the raw logs, so the 137/61/29 counts and the 3/3 CTest result are the parent's report. I read the README, my earlier review, the delta patch, `viewer_controller.cpp` (1002–1060), `settings_file.cpp` (237–316) and the unchanged `window_slot_tests.cpp:175-185`.

## Ranked findings

**F1 (Low–Medium): a terminal save failure is silent once newer feedback has replaced the sort message.**
- **Where:** `viewer_controller.cpp:1048-1049`. The guard is `status==sortFeedback`.
- **Trigger:** Sort is pending behind a foreign lock. The user then navigates, opens a folder or hits an error, so `status` changes. The 4s retry window expires with Busy or WriteFailed.
- **Impact:**
  - `sortSaved=false`, but nothing is shown to the user and nothing is logged.
  - The old code overwrote `status` unconditionally. This closes the clobbering problem but drops the failure notice.
  - The sort stays session-only, and `pendingSortFields` is kept for the next edit.
  - The new `sortCompletionPreservesNewerFeedback` test only covers the success case.
- **Fix:** Keep the ownership guard, but on `!result.ok()` when the guard fails, call `qWarning` as `pane_layout.cpp:121` does. Optionally surface the failure through a non-status channel. No test weakening is needed.
- **Disposition:** Fix or record. Not blocking.

**F2 (Low, test gap): the new tests cover no terminal-failure case.**
- **Where:** `recovery_controller_tests.cpp`, the two new tests.
- **Missing:**
  - A loaded navigator with a failed save, to check that the resort completion composes "could not be saved" from `sortSaved=false`.
  - Failure while the status reads "Sorting…" (the compose-at-delivery path).
  - Failure under newer feedback (F1).
- **Still covered:** `failedSortIntentIsRetried…` exercises on-disk retry, but its status assertion cannot fail (my earlier R3). The failure branch of `setSortFeedback` is effectively untested through the UI path.
- **Fix:** Add a failure-path test, ideally with a lock held past 4s on a loaded navigator. Disposition: record.

**F3 (Low): ownership is a string comparison.**
- **Where:** `viewer_controller.cpp:1049`.
- **Issue:** `status==sortFeedback` is the same fragile token pattern as my earlier N2. Identical text from a newer sort could be rewritten. That is harmless here, because the rewrite is correct for the current persistence state.
- **Disposition:** Residual, no action needed.

## What I checked and found correct

- **Sync callback:**
  - It runs inside `saveWithRetry` while `*deferred` is false. It sets pending=false and saved=ok, and does not touch `status`.
  - The caller then composes the message through `setSortFeedback`, which is correct for both the empty-navigator path (1054) and the resort path.
- **`!accepted` at 1051:**
  - It covers `saveWithRetry` returning false with no completion. The cases are no owner, wrong thread or no edit.
  - If the completion already ran, the line is redundant but harmless.
  - A 128-channel Busy overflow runs the completion synchronously and returns false. That still ends as "could not be saved".
- **Supersession:**
  - `submit` erases the earlier queued "sort" entry before enqueueing (`settings_file.cpp:276-277`).
  - Everything runs synchronously on the GUI thread. `run()` pops, writes and either re-queues or completes in one call, so there is no in-flight window.
  - A superseded edit's completion therefore never fires, and it cannot clear `pendingSortFields` that include newer fields or flip pending=false early.
  - Re-entrant `changeSort` from inside a completion is not reachable.
- **Compose at delivery:**
  - Resort completion (1002) reads the current `sortSaved` and `sortSavePending`.
  - Save finishing before resort gives "Sorted by X.". Resort finishing before save gives "… pending.", which the deferred callback then upgrades. Failure in either order composes correctly.
  - An older resort for a superseded spec briefly shows its own key, because it uses `nav.sort()`. Another resort follows through `needsResort`. This matches the previous behavior.
- **Navigation generations:**
  - A stale resort (token or sequence mismatch) is discarded before 1002, as before.
  - After a stale discard, "Sorting…" can remain until later feedback. That predates this change, and the new guard simply leaves it alone.
  - The sort-completion branch can't coincide with `destination`.
- **Lifetime:**
  - The callback captures only `this` and the shared `deferred` flag.
  - `SettingsRetries` is a child of `q`. Teardown never invokes completions (`:256-264`), and the timer can't fire during destruction.
  - `run()` guards with a `QPointer` after completion.
  - I did not inspect the `Private` destructor order, but the above makes it moot.
- **`contendedSortReportsSessionOnly` (unchanged):**
  - By reading: with an empty navigator and the lock held, the immediate write returns Busy and the edit is queued. `sortSavePending` stays true.
  - `setSortFeedback("File order updated")` yields "File order updated for this session; saving settings is pending." That contains "for this session", and `settings.json` is not created.
  - The old gap was that `saveWithRetry`'s acceptance was read as success. The patch removes it.
  - The delta contains no edit to that test or to its assertions.

## Evidence limits

- Nothing was executed, and I could not confirm the window_slots pass or the 3/3 result.
- I did not read the `fs_watch` failure analysis. The isolated pass does not explain the suite failure, so that remains open.
- I did not check the `Private` destructor ordering.
- There is no full Windows/Docker result for `03bd326`, and no sanitizer, power-loss or physical GPU/DPI evidence.
- No UI or QML changed, so the native Qt design skills did not apply.
