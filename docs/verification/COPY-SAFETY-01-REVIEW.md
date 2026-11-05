# COPY-SAFETY-01 independent review (Windows)

**Verdict: CHANGES REQUESTED.** One blocking data-loss case remains: an Overwrite **Move** whose source and target are the same file reached through two different drive letters (for example `subst`, or a volume mounted both as a letter and as a folder) deletes the only copy. Reproduced on Windows. Everything else in the packet held up, including all 23 `copy_move` functions, the fail-before run, and every item on the Windows verification list. The fix is small (B1).

- Candidate: `origin/claude/copy-safety-01` at `95b0a09`, base `f325ef6`. Reviewed with `git diff f325ef6 95b0a09`.
- Reviewer: a fresh-context Claude Code reviewer that did not implement this packet (AGENTS.md override of October 22).
- Host: Windows 11 Pro 10.0.26200, MSVC 14.44, Qt 6.8.3 msvc2022_64. Drives: C: NTFS, F: exFAT (a second physical volume).
- Worktree: `.worktrees/review-copy-safety`, build directory `build-review-copy-safety` (built through `scripts/build-slot.ps1`).
- Probes: written by the reviewer into a scratch copy of `tests/copy_move_tests.cpp`, then reverted. None are committed. `git status` was clean at `95b0a09` afterwards.

## Blocking

### B1. A same-file Overwrite Move through an alias path deletes the file

`overwriteRefusal` compares `QFileInfo::canonicalFilePath()` strings. On Windows, Qt resolves neither `subst` drives nor junctions. Probe output:

```
JUNCTION canonical alias C:/…/rv_junc_move/alias/file.bin real C:/…/rv_junc_move/real/file.bin
SUBST canonical alias X:/file.bin real C:/…/rv_subst_move/real/file.bin
```

With `subst X: <dir>`, the probe moves `X:/file.bin` into `<dir>` with Overwrite. `isSameVolume` compares drive letters (`X` against `C`) and returns false, so the item takes the copy path:

1. The temp copy replaces `<dir>\file.bin`, which is the source itself.
2. `QFile::remove("X:/file.bin")` then deletes that file.

The service reports success and the file is gone:

```
SUBST copy aliasOk true ok 1 failed 0 msg  file INTACT temps 0
SUBST move aliasOk true ok 1 failed 0 msg  file <missing> temps 0
FAIL!  : CopyMoveTests::reviewSameFileAliases() Compared values are not the same
   Actual   (after): "<missing>"
```

The same path is open to a volume that has a drive letter and is also mounted in a folder, and to a mapped drive that points at a local share. This is by analysis only; I did not test those two. On Linux, a bind mount does the same: `rename` returns EXDEV, the copy fallback runs, then the source is deleted (also analysis only).

Junction aliases survive on the tip. The Move takes the same-volume `MoveFileExW` path, which is a no-op rename onto the same file. The Copy replaces the file with an identical copy. The base code destroyed the file in the junction Copy case (`JUNCTION copy … file <missing>`), so the tip is already better there.

The base code also loses the file in the subst case, so this is not a regression. It is in scope, though. The packet promises that Overwrite never destroys the existing target, and the handoff's section 1 says the same-file case is fixed. Overwrite is not reachable from the UI today: `pasteTo` uses RenameBoth, and only `enqueueTransfer` callers can choose Overwrite. B2 builds on this path, so it should be closed now.

Requested fix:

- Compare file identity, not path strings. On Windows that is `GetFileInformationByHandle` (volume serial number plus file index) or `FILE_ID_INFO`. On POSIX it is `st_dev` plus `st_ino`.
- Do this in `overwriteRefusal`.
- As a second guard, check again just before `QFile::remove(src)` in the Move tail: never delete a source that has the target's identity.
- Add a test. A junction or symlinked folder alias works on both platforms. A `subst` letter can be an optional Windows leg.

## Non-blocking findings

These fail safely, but should be recorded or followed up.

### N1. Paths longer than MAX_PATH

`replaceFileAtomically` passes plain paths to `MoveFileExW`, and the app has no `longPathAware` manifest. An Overwrite of a 357-character target fails with "Could not replace the destination file (Windows error 3). Original kept." RenameBoth on the same folder succeeds, because Qt adds `\\?\` itself. Before this change, Overwrite worked on long paths, destructively. Suggest converting both arguments to `\\?\`-prefixed absolute paths (UNC paths become `\\?\UNC\…`).

### N2. Long file names

The temp name adds 33 characters: `.`, `.glancelot-`, 16 hex digits and `.part`. A target name longer than about 222 characters therefore cannot be overwritten. A 244-character name gave "Cannot open destination file for writing." and the original was kept. Suggest truncating the embedded name in the temp name. The sweep regex only needs the suffix.

### N3. Metadata on the replaced file

The replaced file takes the temp's metadata, not the old target's:

- **Attributes.** H, S and the not-content-indexed attribute (I) become A: hidden `H→A`, hidden+system `H,S→A`, not-indexed `I→A`.
- **Explicit ACEs** are dropped. The probe added `LOCAL SERVICE:(R)`; afterwards only the inherited ACEs remained.
- **Alternate data streams** are dropped. `Zone.Identifier` on the target was gone after the replace.
- **Hard links.** Other links to the old target keep the old content (analysis only).

The handoff documents the ACL and attribute trade-off (design point 4). `docs/REQUIREMENTS.md` (META-07, SYS-06, SYS-02) does not require metadata to be preserved, so this is not blocking. Note one privacy point: a target that the user restricted with an explicit ACL becomes readable by whoever the folder's inherited ACL allows. Suggest one of these for B2:

- copy the target's DACL and its H/S/I attributes onto the temp before the replace;
- or record the trade-off in the user documentation.

### N4. Same-process sweep is keyed on case-sensitive path strings

`inFlightTemporaries` matches exact path strings. Two concurrent batches that spell the same folder in different case, which is possible because batches run on the global thread pool, can sweep each other's temp. The window is between `close()` and `MoveFileExW`. The affected item then fails safely with "file not found" and the target is untouched. Low priority.

### N5. Windows read-only fail-before differs only in the message

On Windows, the base code could not open a read-only target with `Truncate`, so it reported "Cannot open destination file for writing." and lost no data. The Linux fail-before (running as root) did overwrite.

## Code review against the checklist

| Item | Result |
| --- | --- |
| Temp file in the target folder, unique name | Yes. `.<name>.glancelot-<16 hex>.part`, opened with `QIODevice::NewOnly`, up to 8 attempts |
| Complete write, flush and fsync, size check | Yes. `flush()` plus `FlushFileBuffers` or `fsync`. Bytes copied, source size and on-disk temp size must all match. Cancellation is checked before the replace |
| Atomic replace | `MoveFileExW(REPLACE_EXISTING \| WRITE_THROUGH)`; POSIX `rename` plus a folder fsync. `ReplaceFileW` was rejected with sound reasoning (partial-failure codes) |
| Original never truncated or removed unless the replace succeeded | Yes, for distinct files: the target is never opened. Fails for alias paths to the same file (B1) |
| Move deletes the source only after the target is in place | Yes; the existing tail runs after the replace. Same-volume Overwrite Move is a single `MoveFileExW` with a safe fallback |
| Cancellation and every failure path remove the temp | Yes: the `TemporaryCopy` destructor. Verified in each failure test and probe (`temps 0`) |
| Crash-leftover temps | Swept once per target folder per batch, matched by a strict regex; symlinks and in-flight temps are skipped. A real process kill was probed (see below) |
| Non-Overwrite policies unchanged | Yes. Skip and RenameBoth keep the base code (`Truncate` plus remove-on-failure on a newly created name) |
| Generation and cancellation semantics | `batchId`, `batchFinished` and `pathsMutated` are untouched; the cancel checks are kept |
| No change in what the UI can reach | Yes. No QML, header or signal change; `pasteTo` still uses RenameBoth |
| Shared contracts | None changed. `src/app/copy_safety.hpp` is internal. No CMake change |
| MSVC warnings | None in `copy_move_service.cpp` or `copy_move_tests.cpp`, on a clean recompile of both at the tip |

## Windows results

### Targeted ctest

Run inside the gate lock (`gate-lock.ps1 -Branch review/copy-safety`) with `GLANCELOT_CROSS_VOLUME_DIR=F:\glancelot-review-xvol` (exFAT):

```
1/3 Test #15: file_operation_service ...........   Passed    1.24 sec
2/3 Test #34: mutation .........................   Passed    0.17 sec
3/3 Test #35: copy_move ........................   Passed    1.93 sec
100% tests passed out of 3
```

There is no `copy_safety` ctest; the regex matched three tests. A direct run of `copy_move_tests.exe` gave `Totals: 23 passed, 0 failed, 0 skipped`. `testOverwriteMoveCrossVolume` ran against F: and was not skipped. The same three tests passed again after the fail-before swap was reverted.

### Fail-before on Windows

I applied the handoff's instrumentation patch to `f325ef6:src/app/copy_move_service.cpp`. The patch is insertions only (0 removed lines). I placed the result in the review worktree, rebuilt `copy_move_tests` against the tip's tests and ran them: `Totals: 10 passed, 13 failed`. The handoff's 13 failures reproduce:

- 10 show data loss or a stale temp: the target changed during the copy (1), the target is `<missing>` (8), or 2 temps remain (1).
- `testOverwriteReplaceFailureKeepsTargetAndSource` and `testOverwriteMoveSameVolume` fail with `failed 0`, because the base code has no replace step.
- `testOverwriteReadOnlyTargetKeepsTarget` differs only in its message on Windows (N5).

Afterwards I restored the tip files with `git checkout`, rebuilt and reran.

### Handoff Windows verification list

| # | Case | Result |
| --- | --- | --- |
| 1 | MSVC compile | Clean. No warnings in the touched files |
| 2 | Targeted ctest | Pass (above). Read-only gives "Destination file is read-only." and the R attribute stays set. `QFile::resize` on the open source works (the shrinking-source test passes). No temp is left behind in any test |
| 3 | Cross-volume | `testOverwriteMoveCrossVolume` ran with the source on F: exFAT and passed. Extra probes with the **target** on exFAT: Copy, cross-volume Move and same-volume Move all succeed with the target equal to the source's hash. An injected read failure keeps the target ORIGINAL and the source intact, with 0 temps |
| 4a | Target held without delete sharing | For Copy and Move, under each of three share modes (READ; READ+WRITE, as Qt's `QFile` does; READ+DELETE): "Could not replace the destination file (Windows error 5). Original kept." The target stays ORIGINAL, the source is kept and 0 temps remain. After the handle is closed, a retry succeeds (Move also removes the source) |
| 4b | Target shown in the viewer | Not tested in the live app. Approximated by the QFile READ+WRITE holder (fails safely) and a memory-mapped holder (`QFile::map`): the replace succeeds, the target becomes SOURCE and 0 temps remain. Never truncated |
| 4c | `attrib +R` | "Destination file is read-only." The hash and the R attribute are unchanged, for Copy and Move |
| 4d | Hidden or system target | The Overwrite succeeds with the new content in place. The attribute is not kept (N3) |
| 4e | Same-volume and cross-volume Move | Both succeed; the source is removed |
| 4f | Unverified | SMB share: not tested. exFAT: tested, see row 3. Path over 260 characters: fails safely (N1) |
| 5 | Crash leftover | Real process death: a child test process called `TerminateProcess` itself after 256 KiB of a 4 MiB Overwrite. Afterwards the target hash equalled the saved original (`432afdb3…`) and `.target.bin.glancelot-8906452fa1453bc6.part` (262144 bytes) remained. The next Overwrite succeeded, the target became SOURCE, and the leftover was swept (`leftovers QList()`) |

## Housekeeping

- All test data was generated in `QTemporaryDir` folders, under the scratchpad, or under `F:\glancelot-review-xvol`.
- The `subst X:` mapping was removed.
- `F:\glancelot-review-xvol` is left as an empty folder: the reviewer's tool refused to delete it. It is safe to remove by hand.
