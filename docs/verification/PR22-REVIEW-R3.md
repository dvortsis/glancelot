# PR22-REVIEW-R3: ASSET-GROUP contract text, round 3

Verdict: **APPROVED**. Round 2's blocking item B-1 is fixed, and so are its polish items P-1 to P-5. The text is unambiguous, consistent with the contract and the November 5 decisions, and safe for data, so it can be frozen. One non-blocking note, N-1, concerns a reason the decision gives, not a rule.

Reviewer: a fresh-context local Opus subagent. I wrote none of PR 22, none of B1, and neither earlier review. I read only from git objects and ran no builds.

| What | SHA |
| --- | --- |
| PR 22 head (`lead/b1-contract-text`), pinned; `gh pr view 22` shows the same head | `d398d6349831f738545040c1ebc8e782d3e90c97` |
| Round 2 head | `e1bf9e846248453b2ed39198960aaa301e56da75` |
| Base (`origin/main`, merge-base) | `bdef861e4c33f90d9e5de6496ea49ac088236506` |
| B1 code checked against | `0324e943ba37cca642ea4ac902d55af3914b0c6a` |
| B1-REVIEW (`claude/review-b1`) | `e1a4859` |
| Earlier rounds | `docs/verification/PR22-REVIEW.md` (`claude/review-pr22`), `docs/verification/PR22-REVIEW-R2.md` (`claude/review-pr22-r2`) |

Diffs reviewed: `git diff origin/main d398d63` in full (`docs/contracts/ASSET-GROUP.md` +11 −1, new `docs/decisions/2026-11-12-copy-into-own-folder.md`), and `git diff e1bf9e8 d398d63` for the fix round (+5 −3).

## Round 2 findings

| Item | State |
| --- | --- |
| **B-1**: a left-out `Derived` file was keyed apart from its own FullName sidecar | **Fixed.** The text now says the file "is keyed apart from that group, together with its own FullName sidecar if it has one, so `IMG.tif` and `IMG.tif.xmp` stay together". That is round 2's suggested clause. It matches `analyse` at `0324e94` (`asset_group.cpp:267-275`): a left-out Derived file with a FullName sidecar gets its own group, `addGroup(entry->nameKey, pair, 0, {})`, commented "that pair is an asset of its own". That group is added before the stem group, so `groupOfName` maps both files to the pair. The two sentences "keyed by group membership" and "keyed apart from that group" now agree. Round 2's case (skip leaving `IMG.tif` behind and landing `IMG.tif.xmp` beside a different `IMG.tif`; keep both naming it `IMG.tif (2).xmp`) can no longer follow from the text |
| **P-1**: the name set could be read as stem-keyed | **Fixed.** "a set of full file names … They aren't reduced to stems", compared by the case rule in "Stem and case", spelled out |
| **P-2**: two key definitions | **Noted** in the decision ("Also noted"). See N-1 for one sentence there |
| **P-3**: "reported as changed" | **Fixed.** "reported as skipped because the destination changed, and left untouched", which no longer reuses the source-side `Changed` outcome |
| **P-4**: B1-REVIEW P-10 not mentioned | **Fixed.** The decision accepts `cmake/AssetGroupTests.cmake` as it is and makes the move into `cmake/ViewerSources.cmake` (present on `main`) a lead follow-up at integration. The three production lines are at `cmake/AssetGroupTests.cmake:4`, `:5` and `:7` at `0324e94`, as P-10 says |
| **P-5**: the file-scope extension wasn't argued | **Fixed.** "Why" now says why files of one group are skipped or numbered together even in file scope: otherwise skip lands a sidecar beside a different photo, or keep both renames it so it no longer names its file (the November 5 rule) |

## Facts checked against the code and B1-REVIEW

- *Ratified keys and the row field* (unchanged since round 2, re-read): `companionsIncomplete` is set only by the budget or an unreadable parent read, and a cancel returns `Expansion::cancelled`. `name` is on the `companions` rows and on the transfer summary's `collisions`, `targets` and `rejected` rows. Round 2's line references still hold at `0324e94`.
- *Self-collision key*: `transferKey` (`copy_move_service.cpp:571-577`) is the key the move's same-folder check uses (`:613`). It folds case under `Q_OS_WIN` and compares exactly elsewhere, as the decision says.
- *L-1*: `CMakeLists.txt:330` has the one `include(cmake/AssetGroupTests.cmake)` line. `ui/Main.qml` has the `modalOpen` term `transferFlow.modal` (`:437`) and one `TransferFlow {` instance (`:2107`). The clipboard commands are deliberately not ratified in `Main.qml`. The decision moves them into their owning component under the EN-07 rule when B1 merges `main`, which matches AGENTS.md's hot-file rule.
- *L-2, L-3, L-4, I-2, I-3*: each line of the decision matches the item in B1-REVIEW at `e1a4859`. L-3's "before B1 … silently made `name (2)`" is the source of the decision's "Before B1, it made `name (2)` with no prompt".
- *B2 note*: `lead_note_b1` is on the B2 row of `docs/WORK-QUEUE.yaml` on `main` (`:1262`), with the same scope as the contract's new Overwrite sentence.

## Rules B1's round 2 implements

These need not match `0324e94`. I checked that they are unambiguous, consistent and safe for data.

- **Copy into the source's own folder.** Only the first plan of a paste, which is file scope, can run through. It relaxes only the "no collisions" condition. Every other reason to confirm still asks, and the list matches the November 5 rule item for item. Keep both never overwrites, and execution re-checks each target. Group members share one `(n)`, and the `IMG (2).CR2.xmp` example is right for a FullName sidecar.
- **What counts as one asset.** Every requested file has exactly one key:
  - a group member: its group, including the Derived pair group;
  - a left-out Derived file without a sidecar: its own key;
  - `Other` or a folder: its own key.
  
  A destination name that appears after the plan either re-resolves the asset as one or skips it whole. Neither outcome splits an asset or overwrites anything.
- **Plan cost and cancel.** Cancel is checked in every loop, the destination is read once, and a collision check is a lookup in a full-name set under the volume's case rule. The set includes hidden, system and dangling-link entries, so it can't miss a collision. A stem key would cause false collisions, and the text now rules that out.

## Note (non-blocking)

**N-1. "never a missed collision" holds only in one direction.**
- *The text:* the decision's "Also noted" says that where the per-OS folding differs from a volume's case rule, "the only effect is an extra prompt, never a missed collision".
- *Where it holds:* the check under-matches, for example exact comparison on Linux over a case-insensitive volume. A real self-collision then counts as "other", and the dialog asks.
- *Where it doesn't:* `transferKey` folds case on every Windows path, so there it can over-match.
  - Example: a per-directory case-sensitive folder (WSL's feature, or a case-sensitive SMB share) holding sibling folders `Foo` and `foo`.
  - Pasting `Foo\a.jpg` into `foo\`, where a different `a.jpg` exists, folds to the source's own path. It counts as a self-collision, and the paste runs straight through as keep both, with no dialog.
- *Why it doesn't block:*
  - Nothing is lost or overwritten. Keep both picks a free name, checked against the exact-case name set, and execution re-checks it. The user's copy lands in the folder they chose as `a (2).jpg`.
  - The cost is a missed prompt in a rare setup.
  - The contract's rule is unaffected, so this is not a reason to hold the freeze.
- *Optional fix,* in the decision or a later bookkeeping PR: "Where the OS folding is looser than the volume's rule, a real collision can run through as keep both; nothing is overwritten." An implementer who wants to close the gap can compare the parent folders' identity (as `overwriteRefusal` does with `compareFileIdentity`), not only their text.

## Checks run

- `gh pr view 22 --json headRefOid,state,title,baseRefName`: head `d398d63…`, open, base `main`. `git merge-base --is-ancestor origin/main d398d63` passes.
- `git diff origin/main d398d63` and `git diff e1bf9e8 d398d63`.
- I read `git show d398d63:docs/contracts/ASSET-GROUP.md` in full, so the new block sits in the context of the November 5 decisions, the expansion table and the failure table.
- I read both earlier rounds' review documents, and the L-1 to L-4, I-2, I-3 and P-10 items of `B1-REVIEW.md` at `e1a4859`.
- At `0324e94`:
  - `asset_group.cpp:255-300` (the Derived branch of `analyse`) and the `leftOut` and `groupOfName` uses;
  - `copy_move_service.cpp`: `transferKey`, the same-folder check, the asset key and the `(n)` search;
  - `CMakeLists.txt:330`, `cmake/AssetGroupTests.cmake`, and `ui/Main.qml:437` and `:2107`.
- `git grep lead_note_b1` on `main` and on the head.
- No build. This is a docs-only PR, and the brief asked for a review from git objects.
