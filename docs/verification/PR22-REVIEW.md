# PR22-REVIEW: ASSET-GROUP contract text for the B1 review's lead items

Verdict: **CHANGES REQUIRED**. The blockers are about text accuracy, not data safety.

Reviewer: a fresh-context local Opus subagent. I wrote none of PR 22 and none of B1. Read only from git objects.

| What | SHA |
| --- | --- |
| PR 22 head (`lead/b1-contract-text`) | `4497e9681d96c3eece81d472f09fee1c48f137fd` |
| Base (`origin/main`, merge-base) | `bdef861e4c33f90d9e5de6496ea49ac088236506` |
| B1 code checked against | `0324e943ba37cca642ea4ac902d55af3914b0c6a` |
| B1-REVIEW (`claude/review-b1`) | `e1a4859` |

Diff reviewed: `git diff origin/main 4497e96`. It changes `docs/contracts/ASSET-GROUP.md` (+11 −1) and adds `docs/decisions/2026-11-12-copy-into-own-folder.md`.

## What holds

- **L-1.** `TransferFlow` is a direct instance in `ui/Main.qml:2107` at `0324e94`, not inside a `Loader`. So "the clipboard commands are handled in their owning component" fits the EN-07 rule, and the lazy-load exception doesn't apply.
- **L-2.** `git diff e0e76ca 8650d78 -- include/` adds exactly `#include <QVariantMap>` and `#include <QVariantList>`, so "the text above plus the two `#include` lines" is accurate.
- **L-4: the Overwrite note.** It matches decision 2 and B1-REVIEW L-4. `lead_note_b1` exists in the B2 row of `docs/WORK-QUEUE.yaml` on `main`.
- **`companionsNotListed`.** Its meaning is correct: it starts at `excludedNotListed` and adds the rows past the summary's cap (`asset_group.cpp:549-565`). `addCompanionSummary` feeds all three summaries: `file_operation_service.cpp:324`, `copy_move_service.cpp:595` and `mutation_service.cpp:57`.
- **I-2 and I-3.** The plan-cost and one-asset bullets match the review's fixes.
- **The decision document.** It cites the review commit correctly. The L-3 facts it uses ("before B1 it made `name (2)` with no prompt") match B1-REVIEW.
- **Data safety of the copy-into-own-folder rule, under either reading of finding 3.** It holds:
  - Keep both never overwrites.
  - Execution re-checks each target: a late name takes the per-member collision path in `processItem`.
  - Overwrite stays refused until B2.
  - Sources are only read, and nothing is deleted.
  - File-only stays the default, so nothing is included silently.
  - A Move into its own folder is unchanged.

## Findings

### Blocking

**R-1. `name` isn't a summary key.**
- *PR text:* "`name`: the display name the confirmation uses for a single request."
- *The code at `0324e94`:* no `summary.insert` adds a top-level `name`; the full list is in `asset_group.cpp:563-566`, `copy_move_service.cpp:667-674` and `mutation_service.cpp:67-120`. `name` is a field on each row:
  - `companions` rows: `asset_group.cpp:551`, documented in `asset_group_internal.hpp:43` as `{path, name, role, requestedBy, included, exclusion}`;
  - transfer `collisions`, `targets` and `rejected` rows: `copy_move_service.cpp:580`, `:659` and `:661`.
- *The handoff:* B1-HANDOFF decision 11 says "a `name` on each companion".
- *Fix:* name two summary keys. Then say that each `companions` row also carries `name`, the file name the confirmation shows, which the header diff's row list `[{path, role, requestedBy, included, exclusion}]` leaves out. Optionally say the same for `collisions` rows, which `TransferFlow.qml:276` reads.

**R-2. A cancel never sets `companionsIncomplete`.**
- *PR text:* "true when `kExpandBudgetMs`, a cancel or an unreadable parent stopped the lookup."
- *The code at `0324e94`:*
  - After a read stops, `asset_group.cpp:456` calls `cancelled()` before line 458.
  - On a cancel, that replaces `out` with a fresh `Expansion` that has `cancelled = true` and `companionsIncomplete` false.
  - `buildTransferPlan` then returns `plan.cancelled` with no summary (`copy_move_service.cpp:593`). The other services drop a cancelled plan the same way.
- *The contract:* its failure table already maps "Cancel during lookups" to `Expansion::cancelled`, so the new line contradicts it.
- *Fix:* "true when `kExpandBudgetMs` or an unreadable parent stopped the lookup". Optionally add the effect of P-3: after one unreadable parent, the later folders aren't looked up either.

**R-3. The copy-into-own-folder rule has no scope, and one clause doesn't match B1.**
- *PR text:* "When every collision in a copy plan is a source colliding with itself, the paste runs straight through as keep both, with no dialog… Every member of an asset gets the same `(n)`. Any other collision, exclusion, `companionsIncomplete` or refusal still asks."
- *Run-through is file scope only.* A plan that skips the dialog is always the first plan of a paste (B1-REVIEW, decision (a)), and that plan is file scope.
- *How file scope names a duplicate.* `buildTransferPlan` keys each file by its full path. Stem keying applies only under `Scope::Asset` (`copy_move_service.cpp:619-621`). So each request is a one-item asset, named by `completeBaseName` (the `splitOf` lambda).
- *Example.* Select `IMG.CR2` and `IMG.CR2.xmp`, then press Ctrl+C and Ctrl+V. B1 today makes `IMG (2).CR2` and `IMG.CR2 (2).xmp`. The sidecar no longer names its photo, and the two can get different `n` if one name is already taken.
- *What that does to the clause.* "Every member of an asset gets the same `(n)`" is then either vacuous or a new file-scope requirement that B1 doesn't meet.
- *A dropped condition.* The "still asks" list leaves out the November 5 condition "companions (listed or `companionsNotListed`)". In file scope every companion is an excluded row, so "exclusion" probably covers it. But the text doesn't say so, and a reader can take it as a relaxation.
- *Fix: the lead's call, so state both:*
  - (a) this bullet relaxes only the "no collisions" condition of the November 5 rule, and every other condition there stands, companions included;
  - (b) whether requested files that are members of one group stay together in file scope (keyed by group, named by `splitAssetName`, one shared `(n)`), or the clause applies only under asset scope.
- *Same gap in the next bullet.* "What counts as one asset when keep both or skip applies" also needs to say which scope it covers. I-3 was written for asset scope.

### Polish (not blocking)

- **P-a. Define "colliding with itself".** Say the collision's target is the source's own path, compared with the key the `sameFolder` check uses (`transferKey`). Then pasting `a/x.jpg` and `b/x.jpg` into `a/` provably still asks.
- **P-b. Plan cost.**
  - A plan has one destination folder, so "each destination folder" reads oddly.
  - The name set must follow the "Stem and case" rule: case-folded on case-insensitive volumes.
  - It must include hidden and system entries and dangling links. `occupied` checks `isSymLink` today.
  - The destination read itself should stop on cancel.
- **P-c. Which requests are their own key.** "A request with role `Other`, or a folder, is its own key." Also name the other non-members: a Derived file beside a Raw or Rendered file, a video with no partner, and members of a grouping `Collision` group. "Reported as changed" reuses the wording of a source-side outcome for a destination event, so consider a different word.
- **P-d. Decision document.**
  - It says the answers are "now in `docs/contracts/ASSET-GROUP.md`", but L-1 lives only in the decision.
  - "Nothing unlisted is touched" has no list to refer to when no dialog is shown. "Only the copied files and their new `(n)` names" says it exactly.
  - "Every desktop file manager" overclaims. Explorer also names the copy "- Copy", not `(n)`.
  - L-1 doesn't say who moves the clipboard commands, or when.
  - B1-REVIEW P-10 (product build lines in `cmake/AssetGroupTests.cmake`) isn't mentioned, although that file is ratified here.

## Checks run

- `gh pr view 22 --json headRefOid` returned `4497e96…`. The merge-base with `origin/main` is `bdef861`.
- I read `git show origin/claude/review-b1:docs/verification/B1-REVIEW.md` (`e1a4859`).
- I ran `git grep` at `0324e94` across `src/app/` and `ui/` for `companionsNotListed`, `companionsIncomplete`, `"name"` and `summary.insert`.
- I read `asset_group.cpp:410-505` and `:535-566`, `copy_move_service.cpp:586-666` and `:1262-1281`, and `mutation_service.cpp:55-70`.
- I ran `git diff e0e76ca 8650d78 -- include/ | grep '^[+-]#include'` and `git show 0324e94:ui/Main.qml | grep TransferFlow`.
- No build was needed for a docs-only PR.
