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

## Independent source review of61e4752

Actual read-only Sonnet5.5 follow-up completed successfully; raw result, model metadata, prompt/status and report are retained as claude-review-*.json/.txt and CLAUDE-REVIEW-61e4752.md. The reviewer confirmed N1/N4/N5/N6 fixed and found no blocking defect or frozen-contract violation. This is a source conclusion, not integrated approval or a full-gate pass.

Remaining Low observations are recorded rather than changing source during qualification: foreign bindings for other commands are not automatically refreshed into an already-live window after a rejected edit; the confirmed fallback for those other commands can be stale if the saved map later becomes invalid; one final status assertion in the failed-sort test is redundant, while its on-disk intent/foreign-field assertions remain meaningful; the invalid-map conflict wording is pre-existing. No automatic cross-window live-refresh or sanitizer guarantee is claimed. The earlier bounded layout feedback/channel limits remain disclosed above.

The first61e4752 Docker gate reached the engine but failed before any source compilation: BuildKit worker listing returned Unavailable with "error reading server preface: file has already been closed". Full actual output is linux-docker-full.txt. The wrapper's terminal exit was1. This is retained as an infrastructure failure, not a Linux test result. Windows qualification is running unchanged under the shared lock; Docker health is being checked before any retry.

The Docker engine's first default version probe returned500 from v1.55/version after the BuildKit failure. A read-only process-local v1.53 probe and a subsequent default probe both returned29.7.2/exit0 (docker-api-health.txt, docker-default-health.txt). No service restart or persistent configuration change was performed. The identical default Docker command is retried under the main gate-lock; actual output is linux-docker-retry.txt. The original failed output is retained. Pending retry is not a pass.

## Current Windows qualification checkpoint

The full Windows run terminated with exit1:35/37 passed in802.08s. window_slots failed30.81s and fs_watch failed83.78s; folder_grid passed318.82s. Actual output is windows-full.txt. The awaited direct window_slots diagnostic reproduced30pass/1fail: contendedSortReportsSessionOnly expected "for this session" while the writer lock was held. The queued sort adapter had incorrectly treated acceptance for retry as persisted success. A subsequent correction is tracked under ../sort-status-corrections; no old assertion or timing budget is weakened.

The first full fs_watch result was81pass/1fail at noncurrentSameTupleReplacementInvalidatesCache's initial unchanged-source cache-hit assertion. Its awaited isolated diagnostic passed3/3 in1359ms (windows-cache-diagnostic.txt); this does not explain or erase the full-run failure. Watcher source/tests are unchanged from the earlier candidate. Root cause remains unproven.

Full-run recovery snapshots are retained as first-full-recovery*-results.txt, separately from the original committed focused snapshots. first-windows-process retains the35-command process proof, including two22-frame software D3D11/Microsoft Basic Render Driver runs and RGB verification. These are not physical-GPU or input-to-photon results. The queued Docker retry was withdrawn before starting (docker-retry-deferred.txt); its original pre-compilation BuildKit failure and healthy default-engine probe remain preserved. This candidate has neither a full Windows pass nor a Linux qualification pass.
