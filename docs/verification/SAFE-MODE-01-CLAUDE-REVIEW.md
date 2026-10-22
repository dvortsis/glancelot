# SAFE-MODE-01 independent source review (Claude lane)

**Pinned commit:** 0580fe0, product source equal to tested d4f0e70. This was a read-only inspection and I executed nothing.

**Blocking source defect:** none against the frozen engine contract. I did find one regression at the seam with existing callers (finding 1) that I believe should be fixed or explicitly waived by the lead before integration.

## Findings, by severity

### 1. High — preference saves can now fail `Busy` against the window's own checkpoint worker
- **Where:** `src/app/viewer_controller.cpp:490` (`save`), `src/app/settings_file.cpp:213-216`, `src/app/recovery_session.cpp:101`, `src/app/recovery_checkpoint.cpp:110`.
- **Trigger:** within roughly 500 ms of an image decode, the user toggles the tree, filmstrip, theme or a layout setting.
  - The checkpoint worker holds `settings.json.lock` for the whole `transact`.
  - On the UI thread, `updateSettings` makes a single non-waiting attempt (`lockTimeoutMs = 0`).
  - `FileLock` is a share-mode-0 handle on Windows and a per-open-file-description `flock` on POSIX. A second handle in the same process therefore conflicts on both platforms.
- **Invariant violated:** "existing SettingsStatus values and ordinary writer callers stay compatible." Before this packet, position writes ran on the UI thread, so a window could not contend with itself.
- **Impact:**
  - The preference is not persisted and the user sees "could not be saved".
  - `save` (`viewer_controller.cpp:490`) has no `Busy` retry. `positionTimer` retries only position writes (`viewer_controller.cpp:412`).
  - `ThemeManager` (`theme_manager.cpp:49`), `PaneLayout` (`pane_layout.cpp:69`) and `FavoritesModel` (`favorites_model.cpp:77`) call `updateSettings` directly. I did not read how they handle `Busy`.
- **Fix:** give interactive `Busy` a short deferred retry in `save`. Alternatively, have the checkpoint worker hold the settings lock only for the commit.
- **Test:** in `recovery_controller_tests`, use the existing transaction hooks to block a checkpoint mid-transaction. Call a preference setter, release the block, and assert the preference persists.
- **Native failures:** this does not explain them. The fs_watch and grid tests never construct a `WindowSlot` or claim.

### 2. Medium — a failed `beginWindow` permanently disables lazy resume and position saving for that window
- **Where:** `viewer_controller.cpp:1144-1145` (`restoreWindow` waits on `recoveryResumeReady`), `viewer_controller.cpp:264-279` (poll), `recovery_checkpoint.cpp:91-92`.
- **Triggers:**
  - A `recovery.version: 2` written by a newer build.
  - A malformed `recovery` or record entry.
  - A malformed `windows.<slot>`, such as a relative folder. `position()` runs `policy::merge` over the existing slot, so `beginWindow` fails on it.
  - About 400 ms of sessions-lock contention during a concurrent launch.
- **Impact:** `restorePending` is never satisfied. The window shows "Browsing recovery is unavailable" and does not resume, although `windows.<slot>` is readable without the lock and the legacy path resumed from it. Position saving only recovers if a later `accept` re-runs begin.
- **Fail-closed intent:** failing closed is defensible for writes. It is not for a read-only resume, which only needs a validated read.
- **Fix:** fall back to a read-only legacy resume when begin fails, or document the loss explicitly.
- **Test:** begin with a malformed slot record and with a future `recovery.version`. Assert the viewer still resumes or reports the limitation.

### 3. Medium (latent) — owned-position restore desynchronises a live `Session`
- **Where:** `src/app/settings_restore.cpp:70-80` against `recovery_checkpoint.cpp:112` and `recovery_checkpoint.cpp:130`.
- **Trigger:** with a live claim that has begun and checkpointed 1 to N, an `OwnedSlotScope` position restore writes sequence N+1 and a new `checkpointRevision` under the live nonce. The controller's next `accept` is also N+1.
- **Impact:** that checkpoint conflicts (`input.sequence <= row.sequence`). `finishWindow` conflicts on the stale revision, so the window stays dirty.
- **Why latent:** no production caller exists yet. The later recovery UI will hit it.
- **Existing test gap:** `recovery_tests.cpp:733` restores without a prior `beginWindow`, so this path is not covered.
- **Test:** begin, checkpoint 1 to 3, owned restore, accept 4. Expect `Committed` and a clean finish.

### 4. Low — exit-time retry does not wait
- **Where:** `recovery_session.cpp:67-69`, with `options.lockTimeoutMs = 0` at line 61.
- **Cause:** the wait predicate is already true once `closing` is set, so the final checkpoint and the clean mark get 8 immediate attempts.
- **Regression:** the legacy final save waited up to 4 s through `waitOnExit`.
- **Impact:** cross-process contention at exit leaves the window dirty. The next launch treats it as a crash and resumes from the last checkpoint, which is benign but avoidable.
- **Fix:** keep a minimum backoff when closing.

### 5. Low — durability gap in the sealed preimage and backup package
- **Where:** `settings_recovery.cpp:172` and `settings_recovery.cpp:189-192`.
- **Cause:** package members and the manifest use `QFile::flush`, with no fsync and no directory sync, before the manifest rename. The primary uses `QSaveFile`. I did not confirm whether `QSaveFile::commit` syncs in Qt 6.8.3.
- **Impact:** after a physical power loss, the primary could be new while the preimage manifest or members are missing or empty. Process-kill tests cannot detect this.
- **Contract text:** the plan says power loss is unverified, so this is a documentation or hardening item rather than a violation.

### 6. Low — `-qmljsdebugger` omitted from the Qt option table
- **Where:** `app/startup_intent.cpp:19-20`.
- **Cause:** `-qmljsdebugger` is missing from the Qt GUI options the parser now validates up front. If `QGuiApplication` previously consumed it, it now exits 2 with ArgumentError.
- **Check needed:** I could not verify Qt's full option list offline. Compare against Qt 6.8.3 `QGuiApplication` and Qt Quick option handling.

### 7. Low — unexamined in a few places
- `policy::revision()` (`settings_policy.cpp:48`) re-reads the theme catalog and re-serialises 229 commands on every call. Apply calls it up to four times while holding both locks. Correct but wasteful.
- `bounded()` (`settings_maintenance.cpp:45`) can convert a committed restore into a `LimitExceeded` exit if the serialised output grows past the reserve. The reserve looks adequate.
- If stdout fails after a commit, exit is 7 although the restore committed.

## Contract areas checked with no defect found
- **Encodings (sr1, sp1, sf1, ns1):** match the frozen domains, version bytes, little-endian lengths and ordering.
- **Raw reads:** bounded to 1 MiB and revisions bind the raw pair. Absent versus present-empty is distinguished.
- **Restore ordering:** raw pair and namespace are checked under the lock before `NoChange`. The sealed preimage precedes primary replacement. `.bak` and `.damaged` are untouched on restore.
- **Packages:** fixed names, a manifest sealed last, entry and size limits, and no recursion.
- **Locking and ownership:** sessions lock comes before the settings lock everywhere I read. Claims carry nonce, PID, start and namespace. Release revokes the claim. Offline refusal covers live or ambiguous records.
- **Bounds and cancellation:** diff depth, row and byte bounds are charged before allocation, and an over-limit result carries no plan. A cancel after commit returns `Committed`.
- **Preview and preservation:** a corrupt primary with a supported backup is accepted with a disclosure. A future primary is refused. Unknown keys and other slots are preserved.
- **Startup:** early Unicode parsing happens before any `Q*Application`, and maintenance verbs construct only `QCoreApplication`. `--safe-mode` redirects settings before any owner is built. The `GLANCELOT_CONFIG_DIR` redirect is applied before any settings consumer, and no direct `QStandardPaths` writer bypasses `settingsDirectory()`.
- **Teardown order:** `safeEnvironment`, then `windowSlot`, then `viewer`, so teardown is correct.
- **Production hooks:** `GLANCELOT_RECOVERY_TESTING` is defined only on test targets in `cmake/RecoveryTests.cmake`. The recovery test files contain no `QSKIP`, and the recovery cmake has no `EXISTS` guard or skip (`find_package(Python3 REQUIRED)`).

## Could source explain the native failures?
- **folder_grid 400 s timeout and the fs_watch cache-hit assertion:** I found no source explanation.
  - Neither test source references `WindowSlot`, `setWindowClaim`, `restoreWindow` or `setWindowSlot`.
  - The viewer-controller deltas on those paths are the registry local static, the `explicitPositionIntent` flags, the safe-mode status prefix and the binding-resolver move. None touch the decode cache or grid.
  - Both passed in isolation and in the later quiet 37/37, consistent with load from the concurrent monitor-output build. This is consistent with the evidence, not proven.
- **Zero-swap attempt:** not explainable from what I read. `sceneGraphInvalidated` had fired, which implies a window or scene-graph teardown. The unexplained cause stays open.

## Missing contract coverage
- Preference save while a checkpoint is in flight (finding 1).
- Begin failure with a malformed record or a future `recovery.version`, and the effect on resume (finding 2).
- Owned restore followed by a live `Session` checkpoint and finish (finding 3).
- Exit under cross-process contention (finding 4).
- Power-loss durability, which stays unmeasured.

## Inspection limits
- I did not read `tests/recovery_startup_tests.cpp`, `scripts/verify-recovery.py`, the CMake files in full, or `settings_package.hpp`.
- I relied on the recorded registry and layout hash comparisons rather than re-deriving them.
- I did not read the raw process artifacts or the per-test logs. For the quiet Windows run I only confirmed `100% tests passed out of 37` and 578.32 s in `.cache/qualification-update/candidate-windows-quiet.txt:130-132`.
- Every statement about Qt and OS behaviour comes from source reading, not from running anything.

## Recommendation
Source review is **conditionally favourable** on the engine contract. Do not mark the packet done or merge it until all of these hold:
1. The lead decides on finding 1, fixing it or explicitly accepting it. I would fix it, with the test described above.
2. The lead decides on findings 2 and 3, or records them for the recovery UI packet.
3. The integrated commit passes the full Windows and Docker gates and the actual 33-command process proof. The candidate Linux and quiet Windows runs are already 37/37.

I can re-review any fix.

**Targeted native test, if the lead can arrange a slot:** `recovery_controller_tests` with a new blocked-checkpoint-then-preference-save case (finding 1). I started no builds or tests.