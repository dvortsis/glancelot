# PR22-REVIEW-R2: ASSET-GROUP contract text, round 2

Verdict: **CHANGES REQUIRED**. One clause in the new "What counts as one asset" rule needs a fix. R-1, R-2 and R-3 from round 1 are fixed.

Reviewer: a fresh-context local Opus subagent. I wrote none of PR 22, none of B1, and not the round 1 review. I read only from git objects and ran no builds.

| What | SHA |
| --- | --- |
| PR 22 head (`lead/b1-contract-text`), pinned; `gh pr view 22` shows the same head | `e1bf9e846248453b2ed39198960aaa301e56da75` |
| Round 1 head | `4497e9681d96c3eece81d472f09fee1c48f137fd` |
| Base (`origin/main`, merge-base) | `bdef861e4c33f90d9e5de6496ea49ac088236506` |
| B1 code checked against | `0324e943ba37cca642ea4ac902d55af3914b0c6a` |
| B1-REVIEW (`claude/review-b1`) | `e1a4859` |
| Round 1 review (`claude/review-pr22`) | `docs/verification/PR22-REVIEW.md` |

Diffs reviewed: `git diff origin/main e1bf9e8` in full (`docs/contracts/ASSET-GROUP.md` +11 −1, new `docs/decisions/2026-11-12-copy-into-own-folder.md`), and `git diff 4497e96 e1bf9e8` for the fix round.

## Round 1 findings

**R-1 (`name` isn't a summary key): fixed.**
- The text now says there are two summary keys, and that `name` is a field on the rows.
- At `0324e94`, `name` is on each `companions` row (`asset_group.cpp:551`), and on the transfer summary's `rejected` rows (`copy_move_service.cpp:580`), `collisions` rows (`:659`) and `targets` rows (`:661`). The text lists exactly those.

**R-2 (a cancel never sets `companionsIncomplete`): fixed.**
- The text now says the key is true "when `kExpandBudgetMs` ran out or a parent folder couldn't be read", and that a cancel returns `Expansion::cancelled`.
- `git grep -n companionsIncomplete 0324e94 -- src/ ui/` shows only these places that set it:
  - the budget, at `asset_group.cpp:427` and `:497`;
  - a stopped or unreadable read, at `:458`. A cancel during that read leaves at `:456`, before `:458`.
- `cancelled()` (`:418-424`) replaces `out` with a fresh `Expansion`. The failure table's "Cancel during lookups" row agrees.

**R-3 (the copy-into-own-folder rule had no scope): fixed, apart from the new clause below.**
- *(a) What it relaxes.* The rule now says it applies to the first plan of a paste, which is file scope, and that it relaxes only the November 5 "no collisions" condition. Its list of reasons that still confirm matches the November 5 list item for item: companions (listed or `companionsNotListed`), any other collision, an exclusion, `companionsIncomplete` and a refusal. This matches `nothingToDecide` (`TransferFlow.qml:68-71`), apart from the collision condition that B1's round 2 relaxes.
- *(b) File scope.* The lead chose that requested files of one group share one `(n)` in file scope too, and said so in both bullets ("This holds in both scopes").
- *The example checks out.* `splitAssetName("IMG.CR2.xmp", FullName)` splits into `IMG` + `.CR2.xmp`, so the copy is `IMG (2).CR2.xmp`, beside `IMG (2).CR2`.
- *Data safety of the run-through.* Nothing changes from round 1's analysis:
  - Keep both never overwrites.
  - Execution re-checks each target.
  - Overwrite stays refused.
  - Sources are only read.
  - Any companion or exclusion still opens the dialog.
  - Only the first plan, which is file scope, can run through.

## Polish items from round 1

| Item | State |
| --- | --- |
| P-a: define "colliding with itself" | Done: "its target is its own path, compared with the key the move's same-folder check uses", which is `transferKey` (`copy_move_service.cpp:571-577`, used at `:613`). Pasting `a/x.jpg` and `b/x.jpg` into `a/` still asks. See P-2 below |
| P-b: plan cost | Done: one destination folder, cancel during the read, every entry including hidden, system and dangling links. The case rule is named, but see P-1 below |
| P-c: which requests are their own key | Partly done: the left-out `Derived` file is named, but too broadly (B-1). "Reported as changed" is unchanged (P-3) |
| P-d: decision document | Done: L-1 is now marked as recorded only in the decision, with who moves the commands and when. "Most desktop file managers" replaces "every". The cost line no longer says "nothing unlisted". B1-REVIEW P-10 is still not mentioned (P-4) |

## Findings

### Blocking

**B-1. "A `Derived` file left out beside a Raw or Rendered member … [is its] own key" separates that file from its own FullName sidecar.**
- *PR text:* "Requested files are keyed by group membership. A request whose role is `Other`, a `Derived` file left out beside a Raw or Rendered member, and a folder are each their own key".
- *The code at `0324e94`:* `analyse` puts a left-out Derived file and its FullName sidecar into a group of their own (`asset_group.cpp`, the branch commented "that pair is an asset of its own": `addGroup(entry->nameKey, pair, …)`). So "keyed by group membership" puts `IMG.tif.xmp` in the `IMG.tif` pair group, while "its own key" gives `IMG.tif` a key of its own. The two sentences disagree.
- *The case:*
  - The source folder holds `IMG.CR2`, `IMG.tif` and `IMG.tif.xmp`.
  - The destination already holds a different `IMG.tif`, and no `IMG.tif.xmp`.
  - The user selects `IMG.tif` and `IMG.tif.xmp` (file or asset scope), copies, and pastes.
- *What the literal text gives:*
  - *Skip:* `IMG.tif` stays behind, and `IMG.tif.xmp` is copied beside the destination's other `IMG.tif`, where it describes the wrong picture. That breaks the November 5 rule "A sidecar never lands beside a different photo of the same name", and the grouping rule that a FullName sidecar attaches to its exact file. Lightroom or darktable would read its edits onto the wrong image. No file is overwritten or deleted, but this is the metadata-integrity failure the November 5 rule and the new Overwrite note exist to prevent.
  - *Keep both:* the single-item naming (`completeBaseName`, `copy_move_service.cpp:640-641`) gives `IMG (2).tif` and `IMG.tif (2).xmp`, so the sidecar no longer names its file.
- *Today's code:* under asset scope it keys by stem (`:621`), so both files join the `IMG.CR2` asset. The new rule is right to change that; the clause is only too broad.
- *Fix (one clause):* "a `Derived` file left out beside a Raw or Rendered member is keyed apart from that group, together with its own FullName sidecar if it has one". Without a sidecar, "its own key" is already right.

### Polish (not blocking)

- **P-1. The name set's key can be read as a stem key.** "with every entry … keyed by the stem-and-case rule above" can be read as keying the set by stem, since group keys are stem keys under that rule. A stem-keyed set would make `IMG.jpg` look taken when only `IMG.png` is there: a false collision, a dialog and a needless `(n)`. *Fix:* "each full file name, keyed by the case rule in 'Stem and case' (case-folded on case-insensitive volumes)".
- **P-2. Two key definitions.**
  - The self-collision test uses `transferKey`, which folds case by OS: `Q_OS_WIN` folds, other systems compare exactly.
  - The name set uses the per-volume rule.
  - On a case-insensitive volume under Linux, a self-collision then counts as "other" and the dialog asks. That is the safe direction, and keep both never overwrites, so this is only a note for the implementer, who could use one key for both.
- **P-3. "Reported as changed" (round 1 P-c, still open).** That reuses the word for a source-side outcome (`Changed` in the failure table) for a destination event. A distinct word, or a note that it is reported per asset like a late collision, would read better. The wording is unambiguous enough as a requirement.
- **P-4. B1-REVIEW P-10 is still not mentioned.** The decision ratifies `cmake/AssetGroupTests.cmake`, which holds three production build lines (`target_sources(glancelot_platform …)`, `target_link_libraries(glancelot_platform PUBLIC glancelot_core)`, `qt_target_qml_sources(glancelot …)`). The decision should either accept them there or say who moves them.
- **P-5. The file-scope extension isn't argued in the decision.** The decision's L-3 line covers the shared `(n)`. But the contract now also makes skip leave a whole group of *requested* files behind in file scope ("This holds in both scopes"), which I-3 described for asset scope only. That is safe: only listed files are left out, and nothing is overwritten. But the decision gives no reason or alternative for it. One line under "Why" would do, and so would a note that the confirmation should show which non-colliding requested files a skip leaves (I-3's "no sign that skip drops it").

## Checks run

- `gh pr view 22 --json headRefOid` returned `e1bf9e8…`, the pinned head. PR state: open.
- `git diff origin/main e1bf9e8` and `git diff 4497e96 e1bf9e8`.
- I read `git show e1bf9e8:docs/contracts/ASSET-GROUP.md` in full, so the new block sits in the context of the November 5 decisions and the failure table.
- I read `git show origin/claude/review-pr22:docs/verification/PR22-REVIEW.md` and `git show origin/claude/review-b1:docs/verification/B1-REVIEW.md` (`e1a4859`).
- At `0324e94`:
  - `asset_group.cpp:401-569` (`expand`, `addCompanionSummary`), `splitAssetName`, and the `analyse` branches that build `leftOut` and the Derived pair group;
  - `asset_group_internal.hpp`, the summary row comment;
  - `copy_move_service.cpp:545-677` (`transferKey`, `rejection`, `buildTransferPlan`);
  - `git grep -n companionsIncomplete 0324e94 -- src/ ui/`.
- No build. This is a docs-only PR, and the brief asked for a review from git objects.
