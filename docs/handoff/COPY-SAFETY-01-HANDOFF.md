# COPY-SAFETY-01 handoff: Overwrite never destroys the existing target

Track session S9 (cloud, Linux). Rules: AGENTS.md. Queue row: `COPY-SAFETY-01` in docs/WORK-QUEUE.yaml.

## Branch and SHAs

- Branch: `claude/copy-safety-01`
- Base: `f325ef62173620c95a452d24ed0d43bf9902b703` (`origin/main`, "docs(lead): COPY-SAFETY-01 for the Overwrite data-loss path")
- Implementation and tests: `6c172f3df07dbebdf1c1a6efd6f93aec6afc240a`
- Tip: the commit that adds this handoff, directly after `6c172f3`. Review with `git diff f325ef6 <tip>`.

## Files changed

| File | Change |
| --- | --- |
| `src/app/copy_move_service.cpp` | Overwrite path rewritten (temp, sync, verify, atomic replace); refusal checks; stale-temp sweep; platform replace helper; test fault hooks |
| `src/app/copy_safety.hpp` (new) | Internal declarations for the replace helper, temp naming, stale-temp sweep and test-only fault hook. Not a shared contract (`include/glancelot/*` is unchanged) |
| `tests/copy_move_tests.cpp` | 14 new test functions only; existing functions unchanged |
| `docs/handoff/COPY-SAFETY-01-HANDOFF.md` | This page |
| `docs/handoff/COPY-SAFETY-01-base-instrumentation.patch` | Hook call sites only, applied to the base code, to show fail-before (see below) |

No CMake change: the helper lives in the existing `copy_move_service.cpp` (already in `glancelot_platform`), and `copy_move_tests` already has `src/app` on its include path. No header in `include/glancelot/` changed, and no signal, property or enum changed.

## Design

Applies only when the collision policy is `Overwrite` and the item is a regular file. Skip, RenameBoth and the no-collision case keep their code path unchanged.

1. **Refuse before touching anything** (`overwriteRefusal`), when the target exists and is:
   - a folder: "Destination is a folder.";
   - the source itself (same canonical path, for example copying or moving a file into its own folder): "Source and destination are the same file." The base code truncated, then deleted, the file in this case, in both Copy and Move modes;
   - read-only, meaning `!isWritable()` or the owner-write bit is clear (Windows: the read-only attribute): "Destination file is read-only." The owner-bit check makes this hold for root and administrators too; the base code, run as root, overwrote read-only files.
2. **Same-volume move**: one `replaceFileAtomically(src, target)` (rename). If it fails (for example a cross-volume path the volume check got wrong, or a sharing violation), it falls back to the copy path below, which is safe.
3. **Copy path**:
   - Once per target folder per batch, `removeStaleTemporaries` deletes leftover temps from a crash. It deletes only names matching `^\..+\.glancelot-[0-9a-f]{16}\.part$`, never symlinks, and never a temp that a copy in this process is still writing (a mutex-guarded in-flight set).
   - Writes to `.<target name>.glancelot-<16 random hex>.part` in the target's folder (same volume), opened with `QIODevice::NewOnly`, so an existing file is never opened or truncated. Up to 8 attempts are made if a name collides. The dot prefix hides the temp from the folder grid on both platforms (`directory_listing.cpp` treats dot names as hidden).
   - Uses the same 64 KiB chunk loop, pause, cancel and progress signals as before.
   - Then flush plus `fsync` (Windows: `FlushFileBuffers`), close, and verify: no read or write error, not cancelled, bytes copied == source size == temp size on disk. The codebase's other durable writes use `QSaveFile`, whose `commit()` also syncs before renaming.
   - `replaceFileAtomically(temp, target)`:
     - POSIX: `rename(2)`, then a best-effort `fsync` of the folder.
     - Windows: `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
   - On any failure or cancellation the `TemporaryCopy` destructor removes the temp. The target is never opened, truncated or removed, and the source is kept. The messages are "Copy verification failed or cancelled. Source and destination preserved." and "Could not replace the destination file (<OS error>). Original kept."
   - For a move, the source is deleted only after the replace succeeded. This is the existing code after the replace; the cross-volume test checks that the source still exists when the replace runs.
4. **Why `MoveFileExW` and not `ReplaceFileW`**: `MoveFileExW` either replaces the target or fails, leaving both files as they were. `ReplaceFileW` has partial-failure codes (`ERROR_UNABLE_TO_MOVE_REPLACEMENT`, `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2`) that can leave the target renamed to a backup name, and it carries the old file's ACL and attributes over to the new content. The trade-off: the replaced file gets the folder's inherited ACL and the temp's attributes, not the old target's. `src/core/selection_export.cpp` already uses `MoveFileExW(REPLACE_EXISTING)` for its atomic replace.
5. **Fault hooks (test-only)**: `copy_safety::setFaultHookForTesting(hook)` has points Read, Write, AfterChunk, Sync and Replace. Production code leaves the hook unset; each check is a mutex-guarded copy of an empty `std::function`, once per 64 KiB chunk. The hooks also run in the shared chunk loop for non-Overwrite copies, where they are inert.

Semantics kept from the base code: cancellation is checked before each chunk and again just before the replace; a cancel after the replace completed does not undo it, as before. Batch and generation handling (`batchId`, `batchFinished`, `pathsMutated`) is untouched. A successful Overwrite emits the same signals with the same paths.

Behaviour changes to note for review:

- If the target is a symlink, Overwrite now replaces the link itself with a regular file. The base code wrote through the link into the file it points to.
- A read-only target or a same-file Overwrite is now reported as a failed item. Before, it was overwritten, or destroyed in the same-file case.

## Tests (new functions in `tests/copy_move_tests.cpp`)

Every failure case checks the SHA-256 of the original target before and after, and checks that no `*.glancelot-*.part` remains in the target folder. Tests use generated data in `QTemporaryDir`.

| Test | Case | Base f325ef6 (+ hook call sites) | Tip |
| --- | --- | --- | --- |
| `testOverwriteTemporaryNaming` | Temp name shape and same-folder location; look-alike names rejected | PASS (helper only) | PASS |
| `testOverwriteReplacesTargetOnlyAfterCompleteCopy` | Success; after **every** chunk the target is still the original and exactly one temp exists, so a crash at any point leaves the target intact; afterwards target == source, no temp, signals and progress unchanged | **FAIL**: target changed during copy | PASS |
| `testOverwriteSourceReadFailureKeepsTarget` | Injected read error at 256 KiB | **FAIL**: target deleted (`<missing>`) | PASS |
| `testOverwriteShrinkingSourceKeepsTarget` | Real short read: the source is truncated to 200 KiB mid-copy | **FAIL**: target deleted | PASS |
| `testOverwriteWriteFailureKeepsTarget` | Injected write failure at 512 KiB (full volume) | **FAIL**: target deleted | PASS |
| `testOverwriteSyncFailureKeepsTarget` | Injected flush/fsync failure (full volume at flush) | **FAIL**: target deleted | PASS |
| `testOverwriteCancelMidCopyKeepsTarget` | `cancel()` after 1 MiB of a 4 MiB copy | **FAIL**: target deleted | PASS |
| `testOverwriteReplaceFailureKeepsTargetAndSource` | Injected replace failure, Copy and Move; the source is kept | FAIL: base has no replace step, so it overwrote | PASS |
| `testOverwriteRemovesCrashLeftover` | State after a crash (original target plus a partial temp, plus another file's stale temp): the next Overwrite succeeds and clears both; three look-alike user files are kept | **FAIL**: leftovers remain (base knows no temps) | PASS |
| `testOverwriteCrashLeftoverWithFailedCopyKeepsTarget` | Crash leftover, then a failing copy: target intact, leftover and new temp removed | **FAIL**: target deleted | PASS |
| `testOverwriteMoveSameVolume` | Success (one rename, source gone, `isMove` true); and every replace failing, so both rename and fallback fail: target and source intact | FAIL: failure leg not applicable at base | PASS |
| `testOverwriteMoveCrossVolume` | `/dev/shm` (tmpfs) source to the overlay target, or `GLANCELOT_CROSS_VOLUME_DIR`. Success: source still exists at replace time, deleted after. Read failure: target and source intact, no temp on either volume | **FAIL**: target deleted | PASS (ran, not skipped) |
| `testOverwriteReadOnlyTargetKeepsTarget` | Read-only target, Copy and Move: "Destination file is read-only.", target and its read-only state unchanged, source kept | **FAIL**: overwritten (container runs as root) | PASS |
| `testOverwriteOntoItselfKeepsFile` | Copy or move a file into its own folder with Overwrite | **FAIL**: file deleted (`<missing>`) | PASS |

How fail-before was shown: in a detached worktree at `f325ef6`, `docs/handoff/COPY-SAFETY-01-base-instrumentation.patch` was applied. It is insertions only (0 removed lines): the base `copy_move_service.cpp` gains the `copy_safety` helper block and hook calls at the same points (Read, Write, AfterChunk, and Sync just before the base's `flush()`), and its copy logic is otherwise byte-for-byte the base. The tip's `tests/copy_move_tests.cpp` was copied in, and `copy_move_tests` was built and run. Three cases need no hooks at all and fail on the plain base logic the same way: same-file, read-only as root, and crash leftover. Reproduce:

```bash
git worktree add --detach ../cs-base f325ef6 && cd ../cs-base
git apply <tip checkout>/docs/handoff/COPY-SAFETY-01-base-instrumentation.patch
git show <tip>:tests/copy_move_tests.cpp > tests/copy_move_tests.cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=<Qt> && cmake --build build --target copy_move_tests
QT_QPA_PLATFORM=offscreen ./build/copy_move_tests
```

Actual base output (paths shortened):

```
PASS   : CopyMoveTests::testViewerControllerReconciliation()
PASS   : CopyMoveTests::testOverwriteTemporaryNaming()
FAIL!  : CopyMoveTests::testOverwriteReplacesTargetOnlyAfterCompleteCopy() '!targetChangedDuringCopy.load()' returned FALSE. ()
FAIL!  : CopyMoveTests::testOverwriteSourceReadFailureKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
   Expected (fx.targetHash)      : "432afdb325d6d433f5d07f0736cae00066f5d0fc57bd83f575c85577b04a14c8"
FAIL!  : CopyMoveTests::testOverwriteShrinkingSourceKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteWriteFailureKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteSyncFailureKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteCancelMidCopyKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteReplaceFailureKeepsTargetAndSource() Compared values are not the same
   Actual   (outcome.failed): 0
   Expected (1)             : 1
FAIL!  : CopyMoveTests::testOverwriteRemovesCrashLeftover() Compared lists have different sizes.
   Actual   (temporariesIn(fx.targetDir)) size: 2
   Expected (QStringList()) size: 0
FAIL!  : CopyMoveTests::testOverwriteCrashLeftoverWithFailedCopyKeepsTarget() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteMoveSameVolume() Compared values are not the same
   Actual   (outcome.failed): 0
   Expected (1)             : 1
FAIL!  : CopyMoveTests::testOverwriteMoveCrossVolume() Compared values are not the same
   Actual   (sha256Of(fx.target)): "<missing>"
FAIL!  : CopyMoveTests::testOverwriteReadOnlyTargetKeepsTarget() Compared values are not the same
   Actual   (outcome.failed): 0
   Expected (1)             : 1
FAIL!  : CopyMoveTests::testOverwriteOntoItselfKeepsFile() Compared values are not the same
   Actual   (sha256Of(file)): "<missing>"
   Expected (hash)          : "364911a8c76f1e4a40e91c8410db88bad51ab80ed3a317c235428eb8c2f303a6"
PASS   : CopyMoveTests::cleanupTestCase()
Totals: 10 passed, 13 failed, 0 skipped, 0 blacklisted, 369ms
```

## Build environment

Cloud container, Ubuntu 24.04, GCC 14.3. `aqt` got a proxy 403 (`download.qt.io`, `mirrors.dotsrc.org`), and `micro.mamba.pm` was also blocked. Qt is **conda-forge `qt6-main 6.8.3`** in `/opt/qt683`, installed with micromamba 2.3.2 fetched directly from `conda.anaconda.org`. Configure: `cmake -S . -B build-claude-copy-safety-01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/opt/qt683`. The full tree built with no warnings in the touched files. Tests run as root, which is why the read-only check uses the owner-write bit.

## ctest output (tip `6c172f3`, identical code to the handoff tip)

Targeted:

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-copy-safety-01 -R "^(copy_move|mutation|file_operation_service)$" --output-on-failure
    Start 15: file_operation_service
1/3 Test #15: file_operation_service ...........   Passed    0.98 sec
    Start 34: mutation
2/3 Test #34: mutation .........................   Passed    0.05 sec
    Start 35: copy_move
3/3 Test #35: copy_move ........................   Passed    0.66 sec

100% tests passed, 0 tests failed out of 3
```

`copy_move_tests` directly: `Totals: 23 passed, 0 failed, 0 skipped, 0 blacklisted, 667ms`. Three earlier consecutive runs before the commit gave the same 23/0/0.

Full offscreen suite, run once (`QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-copy-safety-01 --output-on-failure`):

```
 1/43 Test  #1: selection_export .................   Passed    0.03 sec
 2/43 Test  #2: perf_trace .......................   Passed    0.03 sec
 3/43 Test  #3: navigation .......................   Passed    0.18 sec
 4/43 Test  #4: directory_listing ................   Passed    0.79 sec
 5/43 Test  #5: file_operations ..................   Passed    0.01 sec
 6/43 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 7/43 Test  #7: viewer_acceptance ................   Passed    5.17 sec
 8/43 Test  #8: thumbnails .......................   Passed    3.69 sec
 9/43 Test  #9: folder_tree ......................   Passed    6.08 sec
10/43 Test #10: pane_layout ......................   Passed    0.11 sec
11/43 Test #11: window_slots .....................   Passed   14.95 sec
12/43 Test #12: folder_grid ......................***Failed  160.48 sec
13/43 Test #13: input_ui .........................***Failed   67.74 sec
14/43 Test #14: pathbar_ui .......................   Passed    2.31 sec
15/43 Test #15: file_operation_service ...........   Passed    0.98 sec
16/43 Test #16: directory_model ..................***Failed    6.72 sec
17/43 Test #17: viewport .........................   Passed    0.03 sec
18/43 Test #18: themes ...........................   Passed    0.23 sec
19/43 Test #19: platform_integration .............   Passed    0.49 sec
20/43 Test #20: grid_search ......................   Passed    0.12 sec
21/43 Test #21: grid_density .....................   Passed    0.08 sec
22/43 Test #22: ui_settings ......................   Passed    0.48 sec
23/43 Test #23: fs_watch .........................   Passed   70.36 sec
24/43 Test #24: fs_watch_budget ..................   Passed   20.24 sec
25/43 Test #25: fs_watch_ui ......................   Passed    1.97 sec
26/43 Test #26: favorites ........................   Passed    0.07 sec
27/43 Test #27: folder_history ...................   Passed    0.02 sec
28/43 Test #28: shortcuts_help ...................   Passed    0.23 sec
29/43 Test #29: filter ...........................   Passed    1.60 sec
30/43 Test #30: cache ............................   Passed    2.12 sec
31/43 Test #31: io_scheduler .....................   Passed    0.06 sec
32/43 Test #32: metadata .........................   Passed    0.03 sec
33/43 Test #33: animation ........................   Passed    0.38 sec
34/43 Test #34: mutation .........................   Passed    0.05 sec
35/43 Test #35: copy_move ........................   Passed    0.72 sec
36/43 Test #36: format ...........................   Passed    0.03 sec
37/43 Test #37: monitor_output ...................   Passed    2.03 sec
38/43 Test #38: recovery .........................   Passed   11.15 sec
39/43 Test #39: recovery_startup .................   Passed    0.19 sec
40/43 Test #40: recovery_controller ..............   Passed   12.18 sec
41/43 Test #41: recovery_process .................   Passed   24.27 sec
42/43 Test #42: monitor_customization ............   Passed    6.87 sec
43/43 Test #43: monitor_customization_ui .........   Passed   14.79 sec
93% tests passed, 3 tests failed out of 43

FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.  (grid_ui_tests.cpp:1646)
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()   (input_ui_tests.cpp:1133)
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.  (directory_model_tests.cpp:163)
```

These three are exactly the known conda-forge Qt baseline failures listed in docs/handoff/PHASE0-CYCLE1-2026-10-22.md, one function each. None of them involves copy/move code. They are not regressions. Docker was not available.

## Windows verification list (for the lead's local reviewer)

None of this could be run on Linux. In a `build-review-copy-safety-01` build on Windows:

1. **Compile under MSVC** (`/W4` applies to `glancelot_core`, not to `glancelot_platform`, but check for warnings anyway). Watch the Windows-only code in `copy_move_service.cpp`: `_get_osfhandle`/`FlushFileBuffers` in `syncToDisk`, and `MoveFileExW` in `replaceFileAtomically`. Also check the `build.yml` `viewer` job for this branch.
2. Run `ctest -R "^(copy_move|mutation|file_operation_service)$" --output-on-failure`. Expect all 23 `copy_move` functions to pass, with `testOverwriteMoveCrossVolume` **skipped** unless set up as below. Points of Windows-specific interest:
   - `testOverwriteReadOnlyTargetKeepsTarget`: the `QFile::setPermissions` read-only attribute must give "Destination file is read-only.", and the attribute must still be set afterwards.
   - `testOverwriteShrinkingSourceKeepsTarget`: `QFile::resize` on the source while the service holds it open for reading (a sharing-mode check). If it fails here, report that; it is not a safety failure.
   - `testOverwriteRemovesCrashLeftover` and every "no temp remains" check: `MoveFileExW` and temp removal work while Defender or the indexer briefly holds the file.
3. **Cross-volume**: set `GLANCELOT_CROSS_VOLUME_DIR` to a folder on another drive (for example `D:\gl-xvol`, or a USB or `subst`-free second volume) and rerun `copy_move`. `testOverwriteMoveCrossVolume` must run (not skip) and pass. Its source is created on that drive and its target on the `%TEMP%` drive.
4. **Real replace semantics.** Manual, with generated files. Record `Get-FileHash` of the target before and after each case, and `Get-ChildItem -Force <dir> -Filter "*.glancelot-*.part"` afterwards; that listing must be empty.
   - a. **Target open without delete sharing**: hold it with `$h=[IO.File]::Open('<target>','Open','Read','Read')`, then do an Overwrite copy from the app or the test (Overwrite is reachable through `enqueueTransfer`; `pasteTo` uses RenameBoth). Expected: the item fails with "Could not replace the destination file (Windows error 32 or 5). Original kept.", the hash is unchanged, and no temp remains. Then `$h.Close()` and retry: it succeeds.
   - b. **Target shown in the Glancelot viewer while it is overwritten**: either succeeds, or fails with "Original kept". Never a truncated or missing file.
   - c. **Read-only attribute** (`attrib +R`): "Destination file is read-only."; the hash and the attribute are unchanged.
   - d. **Hidden or system attribute on the target** (`attrib +H`): the Overwrite succeeds and the new content is in place. `MoveFileExW` has no hidden-file restriction, unlike `ReplaceFileW` and `CopyFile`.
   - e. **Same-volume Overwrite move**: the source disappears and the target has the source's hash. Cross-volume Overwrite move: the same result, through the copy path.
   - f. **Unverified, if hardware allows**: a target on an SMB share; a target on exFAT or FAT32 USB (rename-replace is still replace-after-verified-copy, but not crash-atomic on FAT); a path longer than 260 characters.
5. **Crash leftover by hand**: kill the app (Task Manager) during a large Overwrite copy. The target must still be the original, and a `.<name>.glancelot-*.part` may remain. A later Overwrite into the same folder removes it.

## Not covered or unverified

- All Windows behaviour above (no Windows host here). Physical removable or network volumes are unverified on both platforms.
- A real full volume (ENOSPC) and a real device read error are simulated through the fault hook. The Linux container has no small mount to fill.
- If two Glancelot *processes* overwrite into the same folder at the same moment, one process's sweep can delete the other's in-flight temp. That makes the other item fail safely: on POSIX its rename gets ENOENT and the target is untouched; on Windows the delete fails, because Qt opens files without `FILE_SHARE_DELETE`. Within one process, the in-flight set prevents this.
- The non-Overwrite paths still open their new file with `Truncate`, as before (out of scope, kept unchanged). If a file appears at that name between the collision check and the open, it could still be truncated. Suggested for B2 (OPERATION-JOURNAL): `NewOnly` there.
- A same-volume Overwrite move between two hard links of the same inode is a `rename(2)` no-op on POSIX: it reports success, the source path remains, and no data is lost. Same-path detection uses canonical paths only.
- UI-design skills: not applicable (no UI change).

## Requests for the lead

- None for shared files. If the lead prefers the helper in its own translation unit (`src/app/copy_safety.cpp`), that needs a one-line `glancelot_platform` source addition in the root `CMakeLists.txt`. The declarations are already split out in `src/app/copy_safety.hpp`.
