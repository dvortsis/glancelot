# SAFE-MODE-01 Claude-lane follow-up source review of 7bffd68

I read files only and executed nothing. I reviewed source and tests myself, and I did not read the generated test logs. I treated the narrative claims as claims.

**Disposition: conditionally favourable, not approved for integration.** I found no blocking defect against the frozen engine contract. All three Medium/Low-Medium findings below should be fixed or explicitly waived by the lead. The full Windows and Docker gates are not fresh passes, and power-loss durability stays unverified. I make no integration or done claim.

## Ranked findings

### F1 (Medium): a deferred shortcut conflict is never rolled back in the live window
- **Where:** `src/app/viewer_controller.cpp:1453-1455` (completion), `:1466` (optimistic assignment).
- **Trigger:** `setBinding` passes the local checks and sets `d->bindings[id]=normalized` at `:1466`. The write is queued behind Busy. A foreign window then assigns an overlapping shortcut. The edit lambda sets `conflict` and returns without changing the object.
- **Impact:** the status reads "already assigned…, Clear it first", but `d->bindings[id]` keeps the conflicting shortcut in memory. This window now has two live overlapping bindings that disk does not have.
  - The immediate-path conflict at `:1464` correctly returns false without assigning, so the two paths disagree.
  - The edit lambda also returns `Committed` with no change, so it rewrites the file and `.bak` needlessly.
- **Test gap:** `deferredShortcutStillChecksFreshForeignConflicts` (`tests/recovery_controller_tests.cpp:104`) asserts disk and status only. It never asserts `viewer.bindingFor("fullscreen")`.
- **Fix:**
  - On conflict, restore `before[id]` if the local value is still `normalized`.
  - Emit `bindingsChanged`.
  - Return a no-change decision instead of rewriting.
- **Missing evidence:** a test asserting the in-memory binding after the deferred conflict.

### F2 (Low-Medium): `PaneLayout::load` does not see its own queued edits
- **Where:** `src/app/pane_layout.cpp:38-43` against `:74`, and `src/app/settings_file.cpp:268`.
- **Trigger:** a layout edit queued behind Busy, then a slot change away and back (or a reload) within the 4 s retry window.
- **Impact:**
  - `load()` reads the disk without the pending edit, so the UI shows the old layout.
  - The queued edit lands later, with no reload.
  - Disk and UI diverge until restart.
- **Related issue:** the completion clears or sets `feedback_` and emits `changed()` for the current slot. After a `setSlot`, a stale completion can overwrite the new slot's feedback. A success path also wipes unrelated newer feedback such as "Unlock the layout…".
- **Fix:** capture the slot in the completion and ignore it on mismatch. Reload or merge pending edits when the queue drains.
- **Missing evidence:** a test that changes slots while a persist is deferred.

### F3 (Low): queue edge cases in `SettingsRetries` (`settings_file.cpp:236-296`)
- **Superseded edits are dropped silently.** `submit` erases the previous same-channel edit without calling its `completed` (`:268-269`). Any caller that waits on a per-submission completion would hang. Today's callers tolerate this, but the contract in `settings_retry.hpp` does not say so.
- **The teardown budget can expire silently.** `~SettingsRetries` stops after 4 s. Any batches still pending are dropped with no `qWarning`. The warning only fires for a failed `updateAt`.
- **Sort saves never coalesce.** `changeSort` uses a unique channel per call (`:1022`). A burst of sort changes fills the queue toward the 128 cap, and overflow reports as a failed save.
- **Sort failure state is shared.** `sortSaved` is overwritten by any failed deferred sort, even if a newer one succeeded (`:1024`).
- **Fix:** document these semantics. Give sort a stable channel with merged fields, or keep it as is.

### F4 (Low): begin retry exhaustion keeps position writes dark
- **Where:** `viewer_controller.cpp:271-279`, `recovery_session.cpp:107-116`.
- **What is good:** the read-only fallback is validated and does not authorise writes. It uses a bounded 1 MiB read, a `.bak` fallback only for corrupt data, and `policy::merge` validation of the slot. A failed `beginWindow` no longer blocks resume.
- **Remaining behaviour:** `beginFailureReported` is set once. If a later `accept()` retries begin and succeeds, `recoveryResume` is not refreshed and the status text stays "Browsing recovery is unavailable". This is stale feedback, not data loss.
- **Missing evidence:** a test where begin fails and then a later accept succeeds.

### F5 (Low): policy edge cases for an absent layout
- **Where:** `settings_policy.cpp:137-152`.
- **What is good:** I traced the fix-up logic and it is sound. Empty known-only parents are removed. `version:1` is kept only when unknown content remains. An empty slot entry is removed. The semantics revision string at `:56` is included in the policy hash.
- **Nit:** the fix-up keys on `version` being undefined in the source. A versionless source layout with owned fields is treated as absent. Its valid owned fields are copied, and `version:1` is then re-added if the entry is non-empty. This matches the loader's tolerance more than strict validation, but it is untested.

### F6 (Low, evidence): the `-qmljsdebugger` claim rests on documentation, not on Qt source
- **Where:** `app/startup_intent.cpp:19-20, 68, 70`; tests at `tests/recovery_startup_tests.cpp:104-105, 136-138`.
- **What is good:** inline `-qmljsdebugger=…` is accepted and never forwarded into debugging code by the parser. A separate operand is rejected. It is rejected in maintenance mode too.
- **Unverified:** I cannot confirm from source alone that Qt 6.8.3 consumes only the inline form.
- **Behaviour note:** a user-supplied inline `-qmljsdebugger=port:…` can open a debug TCP listener. That is explicit and opt-in, but it should be called out against the "no network" posture. It should not run inside `--safe-mode`.
- **Check:** maintenance and help paths reject it, and the viewer path leaves it for Qt.

### F7 (Low, coverage): process verifier and diagnostics
- **Layout and Unicode coverage is real.** `verify-recovery.py:220-232` runs layout restore end to end with unknowns and another slot preserved. Unicode config roots are used for source, target, safe-original, restart and layout (`:196, 220, 254, 271`).
- **Digests cover empty directories.** `digest_tree` hashes directories as `"directory"` (`:31-34`).
- **Timing fields exist.** `main.cpp:249-251, 290` records `quit_timer_started_ms`, `quit_callback_ms` and `report_ms`, and the verifier checks their ordering (`:172`). The thresholds are unchanged.
- **Gap:** the failure message at `:181` omits the new timing fields from `render_state`. When `frames` is 0 the message does not show the margin. The JSON is retained, so this is a convenience gap only.
- **Cancellation and non-UTF-8 paths:** cancellation is still covered only by in-process tests. Non-UTF-8 paths are not exercised.

## Earlier findings, audited against this candidate

| Earlier finding | State at 7bffd68 | Evidence |
|---|---|---|
| 1. Preference `Busy` against own checkpoint | **Addressed.** Value-owned edits, per-owner queue (channels coalesce, 128 max), 25 ms retry, 4 s budget. UI-thread attempts never wait. | `settings_file.cpp:236-308`; test at `recovery_controller_tests.cpp:38` |
| 2. Failed begin disables resume | **Addressed.** Validated read-only fallback; writes stay fail-closed. | `recovery_session.cpp:33-54, 107-116`; test at `:127` covers future version and malformed record |
| 3. Owned restore desyncs a live Session | **Addressed.** Restore keeps the sequence and changes the revision. A stale clean acknowledgement still conflicts, by design. | `settings_restore.cpp:70-82`; `recovery_tests.cpp:1015-1030` |
| 4. Closing retry does not wait | **Addressed.** A 50 ms wait per attempt, at most 8 attempts. | `recovery_session.cpp:97-99`; test at `:1032` requires at least 100 ms |
| 5. Package durability | **Still open, disclosed.** Power-loss durability is unverified. | docs correction section |
| 6. `-qmljsdebugger` | Parser accepts it; see F6. | |
| L1. Absent-layout husk | **Addressed**; see F5. | `recovery_controller_tests.cpp:71` |
| L2. Report timing | **Addressed**; see F7. | |
| L3. Process coverage | **Mostly addressed**; see F7. | |

The deferred-save hazards you named all check out as follows:
- **Callers and ordering.** All callers capture values only (`values`, `fields`, `favArray`, `normalized`, `panes`). Superseding moves the edit to the end, so ordering against other channels is preserved.
- **Reentrant completion.**
  - In `run()`, the popped `value` is held on the stack and a `QPointer` guards `this` after `completed` returns.
  - The immediate path does not touch `this` after `completed` returns.
  - The test at `:113` deletes the owner from `stateChanged` inside a completion.
- **Teardown.**
  - The final batch never calls a dying owner's completion, and uses value-owned edits only.
  - It groups by the directory captured at submission, which the namespace test at `:93` covers.
  - The owner thread check fails closed.
- **Foreign shortcut conflicts.** They are re-validated under the lock, but see F1 for the live-window state.

## Neighbouring invariants, with no regression found
- `Session` start, idle and close races are safe. Pending is checked and busy cleared under one mutex hold, so there is no lost wakeup. The destructor's `close(0)` leaves a worker that owns shared state, so there is no dangling reference.
- Sessions-lock then settings-lock ordering is unchanged. The checkpoint, begin and finish paths in `recovery_checkpoint.cpp` keep the same invariants.
- `checkpoint` (`:112`) still requires a dirty state and a higher sequence. `finishWindow` (`:130`) binds the sequence and revision.

## Missing evidence
- A fresh Windows `build.ps1 -Test` and a Docker build on the integrated commit. The parent's 137, 61, 17 and 3 results are targeted, and the historical 37/37 qualifies d4f0e70 only.
- The tests named under F1, F2 and F4.
- Any real power-loss durability measure.
- GPU and physical-hardware behaviour, and Qt 6.8.3 internals such as `QSaveFile::commit` sync behaviour and `-qmljsdebugger` consumption.
- I did not independently re-derive the registry and layout hashes, and I did not read the generated logs.

## Recommendation
1. **Fix F1.** It is a real in-memory correctness gap with a simple fix and test.
2. **Decide on F2 and F3.** Fix them or record them for the recovery UI packet.
3. **Re-run the full gates on the integrated commit.** Under the main gate-lock, one PowerShell process each for `build.ps1 -Test` and Docker.
4. **Send fixes back for a further pinned review.** I can re-review.

No files were edited and no commands were run. I did not write a plan file because this was a read-only review.
