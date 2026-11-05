# COPY-SAFETY-01 independent re-review, round 2 (Windows and Linux)

**Verdict: APPROVED.** B1 is fixed. No probe lost data or left the target missing. That includes the reviewer's own subst Move and an SMB loopback alias (`\\localhost\C$`), which round 0 also destroyed. It also includes a bind mount on Linux, hard links and symlinked folders, and the WSL redirector. The lead's metadata decision holds on Windows: an explicit ACE, `Zone.Identifier` and the hidden attribute all survive an Overwrite. Linux built and passed for the first time (GCC 13, no warnings in the touched files). There are no blocking items. The non-blocking findings below are for the lead to accept or schedule; N1 (crash inside ReplaceFileW) and N2 (backup-name loop) matter most.

- Candidate: `origin/claude/copy-safety-01` at `4005869` (code and tests at `b021a40`). Reviewed with `git diff 95b0a09 4005869`, focused on `c5e184c..b021a40` (round-1 fixes on top of the `origin/main` merge `b4b76f1`).
- Reviewer: a fresh-context Claude Code subagent that implemented none of this packet and did not write the round-1 review.
- Windows: Windows 11 Pro 10.0.26200, MSVC 14.44, Qt 6.8.3 msvc2022_64. Drives: C: NTFS; F: exFAT. Worktree `.worktrees/review-copy-safety-r2`, build `build-review-cs2` (`scripts/build-slot.ps1`, full `scripts/build.ps1`).
- Linux: WSL Ubuntu 24.04, GCC 13.3, Qt 6.8.3 `linux_gcc_64` from `scripts/linux-bootstrap.sh --skip-apt --qt-prefix ~/Qt --build-dir build-cs` (apt packages were installed as root first, because `sudo` needs a password). Native clone `~/gl-cs` at `4005869`; tests ran as the normal user `dvort` (uid 1000), and root only for the bind-mount and ownership fixtures.
- Probes: a scratch `reviewProbe` slot in `tests/copy_safety_tests.cpp` ran one `enqueueTransfer` from environment variables. Fixtures were set up and checked with OS tools only (`subst`, `attrib`, `icacls`, `Set-Content -Stream`, `Get-Item -Stream *`, `Get-FileHash`, `mount --bind`, `ln`, `os.setxattr`, `stat`). The probe slot and every mutation were reverted with `git checkout`. Both trees were clean at `4005869` afterwards, and the final ctest runs used rebuilt, unmutated binaries.

## Blocking

None.

## Non-blocking findings

### N1. A crash inside ReplaceFileW can leave the target name empty

ReplaceFileW is not one atomic rename. It renames the target to the backup name, then moves the replacement into place; error 1177 is the documented proof that the first step can complete without the second. Round 0's `MoveFileExW(REPLACE_EXISTING)` was a single rename.

If the process or machine dies between the two steps:

- the target name is empty;
- the original survives as `.~gb<8 hex>.tmp` beside it;
- that file is never swept (correct), but nothing reports it;
- Glancelot's grid hides dot-names, so the user cannot see it there.

This is not data loss, and it follows from the lead's ReplaceFileW decision. Suggested:

- record the window in the handoff's "Decisions taken";
- have B2 (OPERATION-JOURNAL) recover or report `.~gb*.tmp` files at startup.

### N2. The backup-name loop can reuse an existing name

`replaceFileAtomically` tries 8 random backup names. If all 8 exist, it uses the last one anyway, and ReplaceFileW then overwrites that file. A `.~gb*.tmp` may be the only copy of an original from an earlier unrecoverable 1177.

The reviewer confirmed the overwrite by calling ReplaceFileW directly through P/Invoke. The fixture was:

- `target.bin` holding `AAAA`;
- `repl.bin` holding `BBBB`;
- an existing `.~gb0000beef.tmp` holding `CCCC`.

The call returned success on both NTFS and exFAT. Afterwards:

- `target.bin` held `BBBB`;
- `repl.bin` was gone;
- `.~gb0000beef.tmp` held `AAAA`, so the old `CCCC` was destroyed. Eight 32-bit collisions in a row are not a practical risk. Still, a one-line refusal ("could not choose a backup name") after the loop closes it for free.

### N3. The Move fast path can change the source's metadata before falling back

On the same-volume Overwrite Move, the replacement passed to ReplaceFileW (or `chmod` on POSIX) is the user's source file:

- **Windows.** A real 1177 has already merged the target's attributes, DACL and streams into the replacement. Here that is the source. The code restores the original and falls through to the copy path. If that copy then also fails, the kept source now carries the target's metadata. The `ReplacePartial` fault point cannot show this, because it fakes the 1177 with a plain `MoveFileExW`.
- **POSIX.** `chmod` acts on the source inode before the `rename`, so every other hard link of the source also takes the target's mode. The mode is restored if the rename fails.

No content is lost.

### N4. Non-Overwrite policies are not byte-for-byte unchanged

The queue row says "no change to the non-Overwrite policies". The Move-tail identity guard now runs for every policy.

- A RenameBoth Move through a subst alias behaved correctly in probe P10: `file (2).bin` was created with the right hash, and the source name was removed.
- In the **Unknown** case (same volume, file index 0), a RenameBoth or Skip Move keeps the source but still:
  - counts a success;
  - emits a second `itemFinished`;
  - reports the source as moved in `pathsMutated`.

The "could not delete original source" path already does all three, but this case is new. The deviation is safe. The lead should accept it explicitly.

### N5. Things the code does not preserve (document)

**POSIX: not preserved** (confirmed on Linux):

- the owner and group;
- xattrs: `user.review` was gone after both Copy and Move;
- POSIX ACLs (by analysis).

**POSIX: preserved:**

- the target's `rwx` bits;
- a set-id bit on the source is dropped.

Ownership has a side effect:

- dvort overwriting a root-owned 0666 target in a dvort folder succeeds, and the result is `dvort:dvort 666`. The original owner keeps access only through the mode bits.
- A root-owned 0644 target is refused as read-only.

The handoff mentions owner, group and xattrs in a single line. Please carry this into the user documentation for B2.

**Windows Move:** the replaced file also gains the target's streams that the source lacks. P4 Move ended with both `review.src` and `Zone.Identifier`, as ReplaceFileW documents. That is the conservative direction, because the mark-of-the-web stays.

### N6. Smaller observations

- **Read-only source, same-volume Overwrite Move (P7).** ReplaceFileW needs write access to the replacement, so it fails. The copy path then replaces the target correctly, but `QFile::remove` cannot delete the read-only source. The item counts as a success with "could not delete original source", and the source stays. The base code did the same in the collision case, so this is not a regression.
- **WSL redirector (`\\wsl.localhost\Ubuntu`).**
  - One WSL-to-WSL Overwrite Copy failed safely with "Windows error 5. Original kept." Five reruns all succeeded, so this is intermittent.
  - Identity there uses `GetFileInformationByHandle`. `FileIdInfo` fails with error 50, the volume serial is 0 and the file index is the inode.
  - Same-file aliases (`\\wsl$` against `\\wsl.localhost`) were refused.
- **exFAT.** `FileIdInfo` fails (87), so identity falls back to `BY_HANDLE_FILE_INFORMATION`. The file index is non-zero and distinct, so same-volume exFAT Overwrites work and are not refused as Unknown. An exFAT alias through `\\localhost\F$` was refused as the same file.
- **Grant check.** `git diff --stat b4b76f1 4005869` touches only the packet's files plus `CMakeLists.txt` (one `include` line) and the new `cmake/CopySafetyTests.cmake`. Both are lead-owned under AGENTS.md, so the lead must accept them. The `docs/TESTING.md` label table does not list `copy_safety` (lead follow-up, as the handoff says).

## Code review against the brief

| Item | Result |
| --- | --- |
| Identity read failure refuses safely | Yes, by code reading. `compareFileIdentity` returns Unknown when either handle cannot be opened, or when both are on one volume with file index 0. `overwriteRefusal` refuses Unknown. The Move tail keeps a source whose identity is Unknown, and the folder branch refuses it. Handles are opened with `FILE_READ_ATTRIBUTES` and all share modes, so another program's share mode cannot force Unknown. I found no real-world Unknown case to probe; every volume tried gave a usable ID, including P8, where the target was held open without delete sharing and the identity read still succeeded (Different, then a safe error 32) |
| Re-check before deleting the source (TOCTOU) | Effective. For path aliases (subst, SMB, junction, bind mount) the source name and the target name reach the same directory entry, so the check after the replace still sees Same. Mutation M1 (refusal check off, tail guard on) kept the file in both the subst and SMB Moves with "Same file. Source kept." For a hard link created mid-copy, the check after the replace sees Different, but the target is then a full verified copy, so deleting the source loses nothing. The window left is the gap between the check and `QFile::remove`, which is as small as is practical without handle-based deletion |
| ReplaceFileW 1175/1176/1177 | 1175 and 1176 (both files where they were) report an error. A leftover backup is named in the message. 1177 moves the backup back, and if that fails it reports the backup's full path, writes nothing at the target and stops the Move fast path. The backup is deleted after success. Only the fault-injected 1177 was exercised (the round-1 tests pass). The real-1177 metadata merge is N3; the crash window is N1 |
| Fallback refusal for explicit or protected DACLs | Correct. It runs only on 1, 50, 87 or 120, and only when no backup exists and the target is still in place. It refuses if the DACL is protected, has any non-inherited ACE, or cannot be read. A volume without `FILE_PERSISTENT_ACLS` is let through. Mutation M4 (refusal ignored) makes `testOverwriteFallbackRefusesExplicitPermissions` fail. One gap: on the Move fast path the fallback carries the *source's* ACL, as any same-volume rename does. That ACL can be looser than the folder's, but it is the user's own file |
| POSIX path | `stat` gives `st_dev`/`st_ino` (following symlinks); then `chmod` to `target & 0777`, `rename`, and an fsync of the folder. On EXDEV it falls back to the copy path. Ownership and xattrs: see N5 |
| `\\?\` and `\\?\UNC\` | `winPath` adds `\\?\`, or `\\?\UNC\` for `\\server\share`, after `absoluteFilePath` and `cleanPath`. An existing `\\?\` or `\\.\` prefix is kept. Probe P9: a 446-character path through `\\localhost\C$` succeeded for Copy and Move. Mutation M3 (no prefix) makes the long-path and long-name tests fail with error 3 |
| Temp naming, collisions, cleanup | `.~gl<8 hex>.tmp` with `NewOnly` and 8 attempts. The sweep matches `^\.~gl[0-9a-f]{8}\.tmp$` only. Backups (`.~gb`) are never swept. The in-flight key is case-folded on Windows. No probe or test left a `.~gl` or `.~gb` file behind. For the backup loop, see N2 |
| Cancellation | Unchanged from round 0: checked before each chunk and before the replace (`verified` requires that the batch was not cancelled). Covered by `testOverwriteCancelMidCopyKeepsTarget` on both platforms |
| Non-Overwrite policies | The file copy path, the `Truncate` open and the fast `QFile::rename` are unchanged. The only behaviour change is the Move-tail identity guard (N4) |
| MSVC and GCC warnings | None in `copy_move_service.cpp`, `copy_move_tests.cpp`, `copy_safety_tests.cpp` or `copy_safety_test_support.hpp` (full build on both) |

## Windows results

### Targeted ctest, twice, inside the gate lock

```
& scripts\gate-lock.ps1 -Branch review/copy-safety-r2 -Run { ... ctest --test-dir build-review-cs2 -R "^(copy_move|copy_safety|mutation|file_operation_service)$" --output-on-failure }
```

- PATH was prefixed by `.tools\python\Scripts` and `.tools\Qt\6.8.3\msvc2022_64\bin`.
- `GLANCELOT_CROSS_VOLUME_DIR=F:\glancelot-review-xvol` (exFAT), so `testOverwriteMoveCrossVolume` ran and was not skipped.

Pristine build, before any probe:

```
== git: 400586960a9a1915ec0e18187de2db8a57fdac98 status: []
== run 1
1/4 Test #15: file_operation_service ...........   Passed    1.36 sec
2/4 Test #34: mutation .........................   Passed    0.27 sec
3/4 Test #35: copy_move ........................   Passed    3.02 sec
4/4 Test #36: copy_safety ......................   Passed    1.03 sec
100% tests passed out of 4
copy_move : Totals: 28 passed, 0 failed, 0 skipped
copy_safety : Totals: 11 passed, 0 failed, 1 skipped
SKIP   : CopySafetyTests::fileSymlinkToTargetIsRefused() This account cannot create symbolic links (Windows without Developer Mode).
== run 2
(the same four Passed; copy_move 28/0/0; copy_safety 11/0/1, the same skip)
subst after: []
```

After all mutations had been reverted and both test targets rebuilt, the same two runs gave the same result: 4/4 twice, `copy_move` 28/0/0 and `copy_safety` 11/0/1. (An intermediate run had failed `testOverwriteFallbackRefusesExplicitPermissions`. The cause was that the reviewer had rebuilt only `copy_safety_tests`, so `copy_move_tests.exe` still contained mutation M4. After `copy_move_tests` was relinked it passed. This is recorded so the transient failure is not mistaken for a defect.)

### By-hand probes (`reviewProbe`, OS-tool fixtures)

| # | Case | Result |
| --- | --- | --- |
| P1 | `subst Q:` to the folder. Move and Copy of `Q:\file.bin` into the folder, and of `folder\file.bin` into `Q:\` | All four refused: "Source and destination are the same file." Hash unchanged (`96E00A52FDD6`), one entry `file.bin`, no temporary file. `subst` was empty afterwards |
| P2 | SMB loopback: `\\localhost\C$\…\file.bin` into `C:\…`, and `C:\…\file.bin` into `\\127.0.0.1\C$\…`, for Move and Copy | All four refused as the same file, hash unchanged. The identity read through SMB returned the same volume serial and FileId as the local path. **Round 0 deleted the file in this case** (see fail-before) |
| P3 | `attrib +R` target, Copy and Move | "Destination file is read-only." Hash and `R` unchanged, source kept |
| P4 | Target with an explicit `LOCAL SERVICE:(R)` ACE, `Zone.Identifier` (ZoneId=3) and `+H`. The source has its own `review.src` stream | Copy and Move succeed with the source's hash. `icacls` still lists `NT AUTHORITY\LOCAL SERVICE:(R)`. `Zone.Identifier` is intact, and `attrib` shows `A H`. After Copy, the source's hash, its stream and its ACL are unchanged. After Move, the target holds both streams. Folders contain only `t.bin` |
| P5 | exFAT F:, both files on the same volume, Copy and Move; and the same file via `\\localhost\F$` | Success with the right hash; the source is gone after Move. The alias is refused as the same file |
| P6 | WSL redirector `\\wsl.localhost\Ubuntu\…`: C: to WSL, WSL to WSL, and `\\wsl$` against `\\wsl.localhost` | Success, except one intermittent safe failure (N6). The alias is refused. No leftovers |
| P7 | Read-only source, same-volume Overwrite Move | The target is replaced correctly; the source is kept (N6) |
| P8 | Target held by `[IO.File]::Open(...,'Read','Read')` (no delete sharing), Copy and Move | "Could not replace the destination file (Windows error 32). Original kept." Hash unchanged, no temporary file, source kept |
| P9 | A 446-character target path through `\\localhost\C$` (`\\?\UNC\`), Copy then Move | Both succeed with the right hash; the folder holds only `t.bin` |
| P10 | RenameBoth Move of `Q:\file.bin` (subst) into its real folder | `file (2).bin` created with the original hash; the source name is removed. This is a correct move under a new name |
| — | File symlink as source or target | **Unverified**: the account has no `SeCreateSymbolicLinkPrivilege`, and Developer Mode was not enabled (system setting) |

### Fail-before, reproduced by the reviewer

| Mutation | Result |
| --- | --- |
| **Round 0.** `95b0a09` `copy_move_service.cpp` and `copy_safety.hpp` swapped in, `copy_safety_tests` rebuilt | `Totals: 2 passed, 9 failed, 2 skipped`. The nine failures match the handoff's list exactly: subst `<missing>`; subst folder photo `<missing>`; junction folder `entries size: 1`; hard link `failed 0`; metadata `"attributes H+S+I:lost explicit ACE:lost Zone.Identifier:lost"`; `after.protectedDacl` false; long path `Windows error 3`; long name "Cannot open destination file for writing.". By hand, the **SMB loopback Move** of `\\localhost\C$\…\file.bin` into its own folder reported `ok 1`, and the file is `<missing>` |
| **M1.** `overwriteRefusal` identity check off, everything else at the tip | `substDriveSameFileIsRefused` fails only on the message ("Same file. Source kept."); by hand, the subst and SMB Moves keep the file (hash unchanged). The Move-tail guard alone prevents the loss. `hardLinkIsTheSameFile` and `aliasFolderSameFileIsRefused` fail (`failed 0`) |
| **M1+M2.** The tail guard off as well | `substDriveSameFileIsRefused`: `Actual (sha256Of(file)): "<missing>"`. By hand, the subst and SMB Moves report `ok 1` and the folder is empty. Each guard is load-bearing |
| **M3.** `winPath` without `\\?\` | `overwriteLongPath` and `overwriteLongFileName`: "Could not replace the destination file (Windows error 3). Original kept." |
| **M4.** The fallback ignores explicit DACLs | `testOverwriteFallbackRefusesExplicitPermissions`: `Actual (outcome.failed): 0` |

## Linux results (first compile of the POSIX code)

`scripts/linux-bootstrap.sh … -- -R "^(copy_move|copy_safety|mutation)$"` ran inside `scripts/build-slot.ps1`. The full tree built (594 steps). No warnings in the touched files; the only warnings were the existing `QDateTime::fromSecsSinceEpoch` deprecations in other tests.

```
1/3 Test #34: mutation .........................   Passed    0.35 sec
2/3 Test #35: copy_move ........................   Passed    1.77 sec
3/3 Test #36: copy_safety ......................   Passed    0.60 sec
100% tests passed, 0 tests failed out of 3
```

- **`copy_move`: `Totals: 23 passed, 0 failed, 5 skipped`.** The five skips are the Windows-only fault-point tests. `testOverwriteMoveCrossVolume` (`/dev/shm` tmpfs) and `testOverwriteReadOnlyTargetKeepsTarget` passed.
- **`copy_safety`: `Totals: 10 passed, 0 failed, 2 skipped`.** The two skips are the subst legs. These passed and did not skip:
  - `aliasFolderSameFileIsRefused` and `aliasFolderMoveIntoItselfKeepsEmptySubfolder` (symlinked folder);
  - `hardLinkIsTheSameFile`;
  - `fileSymlinkToTargetIsRefused`;
  - `replaceKeepsAttributesPermissionsAndStreams` (0640 kept);
  - `replaceKeepsRestrictivePermissions` (0600 kept).

Reviewer probes on Linux:

| # | Case | Result |
| --- | --- | --- |
| L1 | `mount --bind real bind` (as root). Move and Copy of `bind/file.bin` into `real`, and of `real/file.bin` into `bind` | All four refused, "Source and destination are the same file." Hash unchanged, one entry. Unmounted; `mounts left: 0` |
| L2 | The target is a symlink to `elsewhere/real.bin`, Copy and Move | The link is replaced by a regular file with the source's hash; `real.bin` is unchanged. This is the documented round-0 behaviour change |
| L3 | `user.review` xattr on the target | Lost after Copy and Move (N5) |
| L4 | Root-owned target in a dvort folder, probed as dvort | 0644: refused as read-only, unchanged. 0666: replaced, ending as `dvort:dvort 666` (N5) |
| L5 | Hard link `a/f.bin` = `b/f.bin`, and the symlinked folder `blink` to `b`, Copy and Move | All refused as the same file; still two links with the same hash |
| L6 | Target 0640, setgid source 2755 | The result is 0640 for Copy and Move; the set-id bit is not carried |

POSIX fail-before (M5: `chmod` skipped in `replaceFileAtomically`):

```
FAIL!  : CopySafetyTests::replaceKeepsAttributesPermissionsAndStreams() Compared values are not the same
   Actual   (int(st.st_mode & 07777)): 420
   Expected (0640)                   : 416
FAIL!  : CopySafetyTests::replaceKeepsRestrictivePermissions() Compared values are not the same
   Actual   (int(st.st_mode & 07777)): 420
   Expected (0600)                   : 384
Totals: 2 passed, 2 failed, 0 skipped
```

Here 420 is 0644, the source's mode. This is the first time a POSIX leg has been shown failing. After `git checkout`, a rebuild and a rerun, the clone was clean at `4005869` and the result was again 3/3: `copy_move` 23/0/5 and `copy_safety` 10/0/2.

## Housekeeping

- Every subst letter was removed (`subst` printed nothing).
- The bind mount was unmounted.
- `~/cs-review-probes`, `\\wsl.localhost\Ubuntu\home\dvort\cs-probe`, `F:\glancelot-review-xvol\p5_*` and the scratchpad probe folders were deleted.
- `F:\glancelot-review-xvol` is left as an empty folder for later cross-volume runs.
- Both review trees are clean at `4005869`.
