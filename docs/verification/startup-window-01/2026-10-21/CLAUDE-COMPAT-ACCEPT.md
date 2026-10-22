**ACCEPT** for whole combined compatibility. I found no required corrections.

I did this with Read, Grep and Glob only. I had no shell, so I did not run `git diff` on `scripts/verify-recovery.py` against `7e88979`. I did not recompute the command-table hashes. I did not read `settings_retry.hpp` or `settings_transaction.hpp`, or the `*-status.txt` files. I relied on the passing output in `combined-target-tests.txt`. I also read only the grepped sections of the two `main-delta.patch` files, not the whole patches.

## Tests read
- **`tests/recovery_tests.cpp:440-472`:** `scopedInputBindingsUseTheSharedRecoveryPolicy` and `viewingModifierMergePreservesUnknownsAndRemovesAbsentKnownValues`.
- **`tests/recovery_controller_tests.cpp:38-74`:** `preferenceSavesRetryWhileOwnCheckpointHoldsWriter`, the combined checkpoint test, which now includes the modifier and binding writes.
- **`tests/recovery_controller_tests.cpp:75-91`:** `restoredModifierAbsenceRestoresDefaultsInTheLiveController`.
- **`tests/recovery_controller_tests.cpp:114-156 and 244-260`:** the retry, foreign-conflict, superseded-binding and owner-deletion tests.

## Implementations read
- **`src/app/binding_validation.hpp`:** whole file.
- **`src/app/command_registry.cpp:1-130`:** the table and `resolveCommandBindings`.
- **`src/app/settings_policy.cpp:85-160`:** wheel-modifier fields at line 104-109, binding fields at 124-127, scoped validation at 155-158.
- **`src/app/viewer_controller.cpp`:** `load` (462-483), `wheelModifiers` and `setWheelModifier` (1417-1445), `setBinding` (1529-1577) and `importSettings` (1761-1773).
- **`src/app/settings_file.cpp:364-394`:** `importSettings`.

## Findings
- **Command table:** `command-table-equivalence.json` records exact text equality. Both SHA-256 values are `e877ac01…b7406`, main `ecf4f41` against candidate `ca907d1`. The registry I read has the INPUT-02 hold, grid and tree entries and the MONITOR entries. The input patch shows the same `usableBindings` and `resolveCommandBindings` logic, and the lines I compared match.
- **Accepted disjoint scopes:** `scopesOverlap` treats canvas and grid as disjoint. `next` on `Ctrl+K` and `grid-open` on `Ctrl+K, Ctrl+L` is therefore accepted. The test checks `Code::Ok`, that the unknown `future-command` is preserved, and that `resolveCommandBindings(...).valid` holds.
- **Rejected overlap:** two canvas commands on `Ctrl+K` and `Ctrl+K, Ctrl+L` give `Corrupt` with no replacement. This goes through the `shortcutOverlap` partial-match path, via `settings_policy.cpp:156`.
- **Hold chord:** `hold-actual` bound to a two-step chord gives `Corrupt`, enforced by the `command->hold && count()>1` check in `binding_validation.hpp:44`. A three-step chord is rejected by `usableShortcut` at the field level.
- **Late defaults:** `presentation-fit` gets no default when `next` is `Ctrl+Alt+F`, covered by `retained.effective` being empty.
- **Viewing merge:** absent `ctrl`, `shift` and `alt` are removed. The unknown `future` key and the new `alt` value survive. An invalid or non-object `wheelModifiers` gives `Corrupt` with no replacement. This matches the field list in `settings_policy.cpp:107-108`.
- **Live-controller restore:** the test can fail for the right reason. `glancelot::importSettings` only copies the source's non-schema keys, and the source contains only `schema`, so it changes nothing. The values asserted afterwards (`ctrl=zoom`, `shift=pan-horizontal`, `alt` equal to `wheelMode`) can only come from `load()` re-reading a file the Viewing apply emptied. They differ from the starting `pan`, `zoom` and `pan-horizontal`. This matches `load()` at lines 478-483 and the `alt` fallback at 1420.
- **Retry and foreign preservation:** both `setBinding` and `setWheelModifier` merge only their own key into the freshly read object under the writer lock.
  - `setBinding` re-validates collisions and `usableBindings` against the foreign state at lines 1547-1551.
  - On a conflict it rolls back to the persisted effective value (1544, 1555, 1559).
  - A newer local edit is not overwritten (1557 and 1565).
  - `deferredShortcutStillChecksFreshForeignConflicts` and `supersededBindingConflictRestoresPersistedValue` check that the file bytes and backup are untouched on conflict, and that the superseded value rolls back to F12.
  - `setWheelModifier` merges only `wheelModifiers[modifier]` and re-checks the current value before reporting failure (1437).
- **No dangling captures:** `setBinding` shares a `shared_ptr<BindingSave>` and captures `before` by value. `setWheelModifier` captures `modifier` and `action` by value. Both pass `this` as the owner. `deferredSaveRetainsNamespace…` shows no callbacks run for a destroyed owner. `deferredBindingNotificationMayDeleteOwner` and `retryExhaustion…MayDeleteOwner` cover deletion from inside the notification. `setBinding` re-checks `alive` before the second emit. In `setWheelModifier`, `alive` is checked only after the call returns, which is safe because the callback runs under the owner guard. The `[&]` captures in the tests only touch locals that outlive the waits.
- **Combined run:** the geometry, window_slots, input_ui, recovery, recovery_controller and recovery_process targets all passed (161 s). This is not a full gate.
- **UI skill (`application-interface-design`):** it requires functional correctness and platform and keyboard behavior to come first. That fits here: Escape is reserved, a hold needs a single key, and disjoint scopes may share a key. No QML changed in this scope.

## Non-blocking notes
- `restoredModifierAbsence…` never asserts the on-disk state after `apply()` and before `importSettings`. The live-controller assertions are enough to catch a regression, so this is optional.
- The `alt == wheelMode()` check is only a weak default check, since both are `navigate`. The `pan-horizontal` to `navigate` change is what actually discriminates.
