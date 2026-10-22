## SAFE-MODE-01 review of 61e4752 (delta 1a205fc..61e4752)

This was read-only (Read, Grep and Glob only). I ran no commands, wrote no files and made no plan. I read the README, my earlier review, the delta patch, and the current `viewer_controller.cpp` (setBinding at 1451–1498, `save` at 503–533, `changeSort` at 1026–1051, the recovery poll at 255–293), `settings_file.cpp` (205–327), `pane_layout.cpp` (72–131) and `command_registry.cpp`'s `resolveCommandBindings`. I also read the test bodies in the patch plus the neighbouring binding call sites in the tests.

**Disposition: N1, N4, N5 and N6 are fixed.** I found no blocking defect and nothing that violates the frozen contract. I have four Low findings to fix or record. This is not an integration approval, because the full Windows/Docker gates and a pinned final review are still pending.

### Ranked findings

**R1 (Low): after a rejected edit, live bindings for other commands stay stale.**
- **Where:** `viewer_controller.cpp:1479-1482` (deferred conflict) and `:1492` (immediate conflict).
- **Trigger:** The foreign writer persists `fullscreen=Z` and `rotate=Y`, and our `fullscreen=Y` edit is rejected.
- **Impact:** `fullscreen` correctly rolls back to Z. Live `rotate` keeps the old value while disk has Y, so the dispatched shortcut differs from the saved one until the next `load()`.
- **Immediate path:** When the conflict is found synchronously, `d->bindings[id]` isn't rolled back at all. `confirmedBindings[id]` (line 1476) is refreshed to the persisted value, so live and confirmed values can diverge.
- **Fix:** On conflict, reuse the success-path import: for keys where live equals `before`, copy `persisted.effective`. In the immediate case also set `d->bindings[id]=rollback`.

**R2 (Low): `confirmedBindings` is refreshed only for the edited id.**
- **Where:** `:1477` and `:1486`.
- **Impact:** Foreign values imported into live bindings never reach `confirmedBindings`. It is only used as a fallback when the persisted map is invalid, so the effect is a stale rollback in that rare case. The per-id branch at `:528` of `save()` is never called (the only caller is `resetBindings`), so it is dead code.
- **Fix:** Update `confirmedBindings` wherever foreign values are imported, or drop the dead branch.

**R3 (Low, test): the final status assertion in `failedSortIntentIsRetried…` can't fail.**
- **Where:** the last assertion, `!viewer.status().contains("could not be saved")`.
- **Why:** With no directory loaded, `changeSort` overwrites `status` synchronously with "File order updated." (`:1045`). The assertion therefore checks nothing about the retry result.
- **Still covered:** The on-disk assertions for key, descending, seed and future are real, so retry and foreign-field preservation are covered.
- **Fix:** Assert `sortSaved` indirectly, or assert nothing about status.

**R4 (Low, UX):** the conflict message for an invalid persisted map reads "Shortcut already assigned to invalid saved shortcut map; reset default shortcuts in another window". This text predates the delta and is not a regression.

### Disposition of each requested item

- **N1: fixed.**
  - **Rollback source:** The rollback comes from the persisted map resolved under the writer lock (`:1464-1465`). It falls back to `confirmedBindings` only when that map is invalid. `resolveCommandBindings` always returns every registry id, so the rollback can never be undefined and can't drop a key.
  - **Superseded edits:** A superseded edit's completion is discarded (`settings_file.cpp:276-277`), so X never confirms anything.
  - **Z scenario:** It traces correctly. `reset` runs immediately, so confirmed starts at the defaults. Y then finds persisted Z and a conflict with `rotate`, and rolls back to Z. Disk stays Z.
  - **Reset ordering:** Reset uses the separate channel `bindings/`. Queue order is preserved, and the reset completion refreshes the whole confirmed map. A set queued behind a reset checks the post-reset map. A set before a reset exits early at `:1478` because live no longer equals `normalized`.
  - **Failure and lifetime:** Non-conflict failures leave confirmed unchanged (correct). Teardown runs edits only, with no completions, and the edits capture only shared values, so there is no dangling `this`. The `QPointer` after the emit at `:1489` is intact.
- **N4: fixed.** Removing the sequence guard is correct, because the queue already discards superseded callbacks. The new test exercises Busy exhaustion, retained `pendingSortFields`, and foreign `seed` and `future` preservation (but see R3).
- **N5: fixed.** Only the exact "unavailable" string is replaced, and the epoch guard is intact. A checkpoint-failure message now survives a begin retry.
- **N6: fixed.**
  - **Recovered-primary repair:** `view.recovered` is excluded from the no-op skip (`settings_file.cpp:225`). The test checks the `.damaged` bytes and the repaired value.
  - **Valid-primary no-op:** A following valid-primary no-op leaves primary and `.bak` bytes untouched.
- **N2: a residual limit, not a contract violation.**
  - **Ownership:** Layout feedback still uses the string `"Saving layout preferences…"` as its ownership token (`pane_layout.cpp:120`). A newer message is preserved.
  - **Failure handling:** A deferred failure under a newer message is now logged (`:121`), and the in-memory session state is unaffected.
  - **Why not a violation:** Nothing in the SAFE-MODE docs I searched requires UI feedback for layout saves.
  - **Remaining fragility:** The string-as-token design is still fragile. A token integer would be the clean fix.
- **N3: a residual limit.**
  - **Overflow:** At the 128-channel cap, `submit` rejects the new edit with a synchronous Busy completion (`:278-281`). Callers show failure feedback, so overflow is visible, not silent.
  - **Supersede-before-cap:** Supersede runs before the cap check, so a replaced edit can't push the queue over.
- **N7: a residual limit, still unverified.**
  - **Reentrant deletion:** Handled in code and covered by the test.
  - **Missing evidence:** Sanitizer evidence is absent.

### Neighbouring invariants

- **Generation and stale results:** Superseded callbacks never fire. The `d->bindings.value(id)!=normalized` check stops a stale completion from overwriting a newer local edit.
- **Bounded:** The queue cap of 128, the 4s per-edit window and the 4s teardown budget are all intact. All edit closures capture values only.
- **Foreign and unknown fields:** These are preserved by field-level merges.
- **Original media:** No path in the delta touches original media.
- **UI:** No UI or QML changes, so the native Qt design skills did not apply.

### Evidence limits

- I executed nothing. I did not open the raw logs (`windows-build.txt`, `windows-focused.txt`). The 137, 61 and 26 case counts and the 3/3 CTest result are the parent's report.
- I did not read the full frozen contract document end to end. The "no contract violation" conclusion rests on keyword searches of the docs plus the delta.
- There is no fresh full Windows or Docker result for 61e4752, and the older 37/37 run is not current qualification.
- Power-loss, physical GPU/DPI and sanitizer evidence is unverified.
