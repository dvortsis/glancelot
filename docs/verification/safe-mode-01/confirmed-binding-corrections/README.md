# SAFE-MODE-01 confirmed binding corrections

The read-only Sonnet5.5 review of1a205fc completed successfully in13turns/131925ms. Its complete report/model metadata are retained under followup-corrections. It found N1: rolling back a second queued shortcut edit could restore the first superseded optimistic value. Earlier F1–F4 fixes were otherwise confirmed, with Low observations recorded below.

N1 is corrected by keeping confirmed bindings separate from optimistic UI values. The setter resolves the actual persisted map under the writer lock and uses its value for rollback; if that map is invalid, it uses the last confirmed value. Successful direct/reset saves and valid reloads refresh confirmed state. Superseded optimistic values are not confirmation. The new test queues X thenY, lets a foreign writer persist Z for the same command and takeY for another command, then asserts both live and disk useZ after rejection.

N4: removed the redundant sort sequence guard. The one stable queue channel already discards superseded callbacks. A new failure-path test exhausts Busy, makes a foreign sort edit, then accepts a new local direction edit. Retained local key intent is retried while unrelated foreign seed/unknown fields remain. Retrying retained owned fields may replace a foreign change to the same owned field; this is intentional local pending intent, not a claim that every foreign edit wins.

N5: successful begin retry now replaces only obsolete unavailable feedback, preserving an actual checkpoint-failure message. N6: a new private retry test proves that an identical semantic edit still repairs a corrupt primary from the supported backup, retains the damaged bytes, and that the subsequent supported-primary no-op leaves primary/backup bytes unchanged. Ordinary updateSettings semantics remain unchanged.

Recorded Low limits/observations:

- N2: layout feedback conservatively preserves a newer slot/namespace/message. If a deferred write fails while that message owns the UI, it now logs a failure warning rather than silently dropping it; it does not overwrite the newer message. Ownership still uses the existing saving-message marker rather than a separate public state/token. No durability acknowledgement is implied by applying a layout in memory.
- N3: per-owner retries remain bounded to128distinct channels across slots. Overflow is rejected with existing failure feedback; session edits may remain unsaved. No unbounded fallback queue is added.
- N6: supported-primary no-ops intentionally do not refresh the backup or normalise a legacy missing-schema file. Recovered-primary repair is explicitly excluded from no-op skipping and is tested.
- N7: reentrant notification deletion has a behavioral regression; sanitizer, physical GPU/DPI and power-loss evidence remains unavailable/unverified.

Actual focused build/command uses the same restricted standard build runner and project-local Qt/Python PATH as preceding corrections. Full raw output is windows-build.txt and windows-focused.txt:
```
1/3 Test #34: recovery ......................... Passed 18.12 sec
2/3 Test #35: recovery_startup ................. Passed 0.34 sec
3/3 Test #36: recovery_controller .............. Passed 14.54 sec
100% tests passed out of 3
Total Test time (real) = 33.29 sec
```
Raw QtTest files retain137recovery,61startup and26controller passes (224total), zero failures/skips. No assertions, renderer settings, frame/RGB proof or timing budgets were weakened. No shared include/glancelot header, root CMake, queue, other lane worktree or main was edited.

The obsolete1a205fc full Windows/Docker jobs were cancelled while queued, before either acquired the gate or spawned a gate command. Exact wrapper identities/wait output are retained under followup-corrections. They do not qualify this source. A further pinned independent review and fresh full qualification are still pending.
