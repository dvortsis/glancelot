# PR 19 review, round 3: Conventional Commits and SemVer

**Verdict: CHANGES REQUIRED**

- **PR:** 19, `ci(guard): enforce Conventional Commits and record the release policy`, on branch `lead/conventional-commits`.
- **Reviewed head:** `f0b35ea5a5658f98277257f08c9ae355ad522270`. Round 1 (`PR19-CONVENTIONAL-COMMITS-REVIEW.md`) reviewed `c405156`, and round 2 (`-R2.md`) reviewed `e3f2ba1`.
- **Diffs reviewed:**
  - `git diff e3f2ba1 f0b35ea`: 5 files, 49 insertions, 12 deletions.
  - The whole PR, `git diff origin/main...f0b35ea`, with `origin/main` at `20f1210`: 10 files, 354 insertions, 12 deletions.
- **Reviewer:** a fresh-context Opus subagent that wrote none of this work and neither earlier review.
- **Where the review ran:**
  - Code was read in a detached worktree, `.worktrees/review-pr19-r3`.
  - The PR simulations ran in a throwaway clone under the session scratchpad, with every scratch commit dated by `GIT_COMMITTER_DATE=2026-11-07T10:00:00Z`.
  - The hook runs used a throwaway repository with `git -c core.hooksPath=scripts/hooks`.
  - The configuration of `C:\src\Github\Glancelot` was not changed.

All six round-2 findings (N1 to N6) are fixed as written. Each new check has a test that catches its removal.

Reading the whole PR afresh found one blocking gap. The legacy exemption accepts exactly one pre-rule style, `<TASK-ID>: <title>`. The branches it exists for use several other styles, and PR 18's own branch already uses one of them twice. Those sessions run under their branch's pre-rule `AGENTS.md` and `pr_guard.py`, which set no commit-subject rule and lint no commits. So:
- nothing warns them before they push;
- once such a commit is pushed after November 6, the PR's `guard` is red, and only rewording shared history would clear it.

This is the same class of problem as round 1's finding 1, so it blocks on the same terms.

## Findings, by severity

### R3-1. High (blocking): legacy PRs and integration batches still go permanently red on the branches' own pre-rule subject styles

`commit_lint.LEGACY` is `^[A-Z][A-Z0-9-]*: \S`. With `legacy=True`, `message_errors` accepts that pattern and nothing else. Every other subject still goes through the full Conventional check: lowercase scope, the type list, at most 100 characters, a blank second line.

The branches PRs 7 to 18 run on predate any commit-subject rule. The old `AGENTS.md` set only the PR title format. Their histories mix styles. Here are pre-rule commits linted with `legacy=True` and the cutoff ignored (script `legacy_styles.py` in the scratchpad):

```text
== origin/main~150..origin/main: 109 of 485 pre-rule commits refused even with legacy=True
   da2280a 'test(STACK-CONFIRM-FIX): wait out two load-sensitive races (review R5-N1, R5-N2)': scope 'STACK-CONFIRM-FIX' must be lowercase words joined by hyphen
   caa6a6a 'fix(contracts): review round, trash probe, transfer rules, refresh, items, preimages, share key, key pass': the subject is 105 characters; keep it to
   607546c 'i18n: en_XA catalog for the chip description': use 'type(scope): description', ...
   a0952b6 'wip: drop vanished-file case, keyboard hint in grid description': type 'wip' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, r
   36ca8a6 'fix(COPY-SAFETY-01): refuse instead of reusing an existing backup name (review r2 N2)': scope 'COPY-SAFETY-01' must be lowercase words joined by hyph
   ...
== origin/main..origin/claude/en-07: 2 of 6 pre-rule commits refused even with legacy=True
   c15a0db 'test(EN-07): add the command_handlers_ui target and a loupe order test': scope 'EN-07' must be lowercase words joined by hyphens
   4e3fa36 'test(EN-07): use a 10 s deadline in the pane guard case': scope 'EN-07' must be lowercase words joined by hyphens
== origin/main..origin/claude/b1-asset-ui: 0 of 10 pre-rule commits refused even with legacy=True
== origin/main..origin/claude/a1-load-more: 0 of 21 pre-rule commits refused even with legacy=True
```

PR 18's session writes `test(EN-07): ...` for a third of its commits. Its next review-fix round is likely to do the same after the cutoff. The simulation below uses PR 18's real tip, `origin/claude/en-07`, and a simulated `main` after PR 19, `5b8ad7c` (`20f1210` plus a GitHub-style merge of `f0b35ea`). It runs f0b35ea's `pr_guard.py` with CI-style event files:

```text
-- PR 18 merged with main + 'test(EN-07): fix review finding': PR 18, claude/en-07, guard from f0b35ea
::error::commit 1598726a5: 'test(EN-07): fix review finding': scope 'EN-07' must be lowercase words joined by hyphens
pr_guard: code=True, 1 error(s)
exit=1
-- PR 18 NOT merged with main (merge ref) + 'test(EN-07): fix review finding': PR 18, claude/en-07, guard from f0b35ea
::error::commit 52724a386: 'test(EN-07): fix review finding': scope 'EN-07' must be lowercase words joined by hyphens
pr_guard: code=True, 1 error(s)
exit=1
-- lead/integrate-30 batching the test(EN-07) head: PR 30, lead/integrate-30, guard from f0b35ea
::error::commit 1598726a5: 'test(EN-07): fix review finding': scope 'EN-07' must be lowercase words joined by hyphens
pr_guard: code=True, 1 error(s)
exit=1
```

Without merging `main`, the commit still fails. The `guard` job's `actions/checkout@v4` has no `ref:`, so a `pull_request` run checks out `refs/pull/<n>/merge`. Once PR 19 lands, PR 18's CI runs the new `pr_guard.py` straight away.

The session cannot see this coming. Its required pre-push check runs its own branch's guard, and none of the open legacy branches has `commit_lint.py`:

```text
-- PR 18 session's own pre-push check (its branch's guard, 3b7283e), with the test(EN-07) commit
   ls: cannot access 'scripts/ci/commit_lint.py': No such file or directory
pr_guard: code=True, 0 error(s)
exit=0
claude/b1-asset-services has commit_lint.py: no
claude/b1-asset-group has commit_lint.py: no
claude/a1-perf-enum has commit_lint.py: no
claude/en-07 has commit_lint.py: no
```

After the push, nothing sanctioned clears the red:
- AGENTS.md forbids rewording a shared branch or force-pushing it.
- `ci-known-red` does not excuse `guard`.
- Cloud sessions cannot be messaged.

The landing lane does not help, because the commit is in the PR's own range. The only way out is a new branch with reworded commits, which means a new PR. That PR is numbered 19 or higher, so every post-cutoff `EN-07: ` commit in it must be reworded as well, and the reviews pinned to the old SHAs must be redone.

**Fix (author's choice):**
- **(a) Recommended.** In legacy mode (`is_legacy_pr(number)` or `is_integration_branch(head_ref)`), skip the subject check on commits entirely. Those commits were written under rules that had no commit-subject format, and the PR title, which becomes the merge commit's subject, is still checked. Update the RELEASING.md sentence "Every other format is still refused there" to match.
- **(b)** Widen what legacy mode accepts: any type, any scope case, no length limit. This is enumeration, and the next unlisted style would strand a PR again.

Either way, add a test that uses a `test(EN-07): ...` subject, and preferably a 101-character one, not only `EN-07: ...`. With (a), keep the existing assertion that `update stuff` on PR 18 is refused only if you keep some legacy check; otherwise change it.

### R3-2. Low (non-blocking): an integration batch accepts every legacy-form commit, including new work

`commit_errors` turns on `legacy` for any `lead/integrate-*` head. That covers the batched legacy packets as intended. It also covers two other kinds of commit:
- a `<TASK-ID>: ` commit from a packet numbered 19 or higher, which is red on that packet's own PR;
- a fresh commit the lead makes on the batch branch itself.

`lead-merge.ps1` checks only the batch PR's `ci-ok`, not the batched packets':

```text
-- PR 30 on claude/x with the same commit subject: PR 30, claude/x
::error::commit 8a7268099: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
-- lead/integrate-31 batching new PR claude/x with 'EN-07:' commit: PR 31, lead/integrate-31
pr_guard: code=True, 0 error(s)
exit=0
```

The exemption is also keyed on the branch name alone, like the existing `wait_errors` and `size_errors` exemptions, and only the lead pushes `lead/` branches. That makes this a lead-discipline gap, not a merge blocker.

Fix options:
- add a line to AGENTS.md ("Keeping PRs from colliding"): batch only packets whose own `ci-ok` is green;
- or print a `::warning::` that lists the legacy-form commits a batch accepted.

R3-1's option (a) widens this exemption to every subject, so the warning becomes more useful if (a) is chosen.

### R3-3. Low (non-blocking): CI's own PR-number path is untested

`number = event.get("number") or args.pr`. The new test `test_main_passes_the_pr_number_through` drives only `--pr`. Replacing the line with `number = args.pr` breaks the CI path, because CI passes no `--pr`, and the tests still pass:

```text
== main() ignores the event's number
Ran 45 tests in 5.205s
OK
```

The event path works today: the PR 18 simulation below passes with an event file and no `--pr`. Fix: add an event-file case to that test, setting `GITHUB_EVENT_PATH` to a temp JSON with `number: 18` and no `--pr`.

### R3-4. Low (non-blocking): with the hook on, a merge commit cannot be amended

N5's fix removed `"Merge "` from `LOCAL_ONLY`. The hook exempts a merge only through `MERGE_HEAD`, and `git commit --amend` on a merge has no `MERGE_HEAD`:

```text
[amend --no-edit on that merge (no MERGE_HEAD)] exit=1
    commit_lint: "Merge branch 'side'": use 'type(scope): description', ...
[amend -m 'merge: ...' on that merge] exit=1
    commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
```

The hook is opt-in, and `guard` skips merges. A hook user can redo the merge, or skip this one hook run. Fix options:
- in the hook, exit 0 when `HEAD` has a second parent and the commit being amended is a merge;
- or say in RELEASING.md that amending a merge needs `--no-verify`.

### R3-5. Informational: a new PR stacked on a pre-PR-19 legacy branch is not linted until it is retargeted

A PR's `guard` runs the copy of `pr_guard.py` in its merge ref. For a new PR (19 or higher) based on, say, `claude/b1-asset-services`, that copy is the base branch's pre-rule guard, which lints no commits.

The session on that branch also reads the base branch's pre-rule `AGENTS.md`. Its `B1: ` commits stay green until the parent lands and GitHub retargets the PR to `main`. Then they go red for good. This is correct by policy, because that is new work, but it is caught late.

`lead-merge.ps1` uses `--merge`, so the parent's commits are reachable from `main` after retargeting and are not in the range. The RELEASING.md sentence about stacked PRs therefore holds. Suggest one line: a new PR stacked on a legacy branch merges `main` into its base first, or its brief states the commit format.

## Round-2 findings: resolution

| # | Round-2 finding | Status | Fail-before (mutation, then test) |
| --- | --- | --- | --- |
| N1 | Integration batches refuse legacy commits | **Fixed** for `<TASK-ID>: ` (other styles: R3-1) | dropping `or is_integration_branch(head_ref)` fails `test_old_prs_and_integration_batches_accept_task_subjects` |
| N2 | Legacy wiring untested | **Fixed** (event path: R3-3) | `main()` no longer passing `legacy`, and `range_errors` ignoring `legacy`, both fail tests |
| N3 | No `--pr` for local runs | **Fixed** | `main()` ignoring `--pr` fails `test_main_passes_the_pr_number_through` |
| N4 | `Revert "..."` PR title refused | **Fixed** | removing the `REVERT` check in `title_errors` fails `test_git_revert_subject_passes` |
| N5 | Hook accepts a non-merge `Merge ...` | **Fixed** (side effect: R3-4) | putting `"Merge "` back in `LOCAL_ONLY` fails `test_merges_and_local_fixups`; hook run below |
| N6 | `Reapply "..."` refused | **Fixed** | `(Revert\|Reapply)` changed back to `Revert` fails `test_git_revert_subject_passes`; hook run below |

Mutations ran one at a time on a scratch copy of `scripts/ci`, which was restored after each:

```text
== N1: drop the integration-branch exemption
FAIL: test_old_prs_and_integration_batches_accept_task_subjects (__main__.CommitRange.test_old_prs_and_integration_batches_accept_task_subjects)
FAILED (failures=1)
== N2/N3: main() ignores --pr
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAILED (failures=1)
== N2: main() stops passing legacy
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAILED (failures=1)
== N2: range_errors ignores legacy
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAIL: test_old_prs_and_integration_batches_accept_task_subjects (__main__.CommitRange.test_old_prs_and_integration_batches_accept_task_subjects)
FAILED (failures=2)
== N4: title_errors drops REVERT
FAIL: test_git_revert_subject_passes (__main__.CommitExemptions.test_git_revert_subject_passes)
FAILED (failures=1)
== N6: Reapply not accepted
FAIL: test_git_revert_subject_passes (__main__.CommitExemptions.test_git_revert_subject_passes)
FAILED (failures=1)
== N5: Merge back in LOCAL_ONLY
FAIL: test_merges_and_local_fixups (__main__.Commits.test_merges_and_local_fixups)
FAILED (failures=1)
== restored
Ran 45 tests in 5.691s
OK
pristine
```

## Requested simulations

Simulated `main` after PR 19 is `5b8ad7c`. PR 18's landing-lane merge is `2b853a1`, with subject `merge: origin/main into claude/en-07 (lead, landing lane)`. Each run uses a CI-style event file, with the head's own `pr_guard.py` from f0b35ea:

```text
-- PR 18 merged with main + 'EN-07: fix review finding': PR 18, claude/en-07, title 'EN-07: per-component command handling', guard from f0b35ea
pr_guard: code=True, 0 error(s)
exit=0
-- lead/integrate-30 batching that PR 18 head: PR 30, lead/integrate-30, title 'chore(integrate): batch 30 (EN-07)', guard from f0b35ea
pr_guard: code=True, 0 error(s)
exit=0
-- PR 30 on claude/x with the same commit subject: PR 30, claude/x, title 'feat(x): thing (EN-07)', guard from f0b35ea
::error::commit 8a7268099: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
```

All three match what the brief requires. The R3-1 and R3-2 runs above come from the same clone.

## Hook (`scripts/hooks/commit-msg` at f0b35ea, git 2.56.0.windows.4)

```text
[A1: bad subject] exit=1
    commit_lint: 'A1: bad subject': use 'type(scope): description', ...
[update stuff] exit=1
    commit_lint: 'update stuff': use 'type(scope): description', ...
[test(EN-07): x] exit=1
    commit_lint: 'test(EN-07): x': scope 'EN-07' must be lowercase words joined by hyphens
[Merge the two caches (not a merge)] exit=1
    commit_lint: 'Merge the two caches': use 'type(scope): description', ...
[fix(grid): good subject] exit=0
[fixup! fix(grid): good subject] exit=0
[git merge, default subject] exit=0
    subject: Merge branch 'side' parents: 26aa115 d72edfb
[amend --no-edit on that merge (no MERGE_HEAD)] exit=1
    commit_lint: "Merge branch 'side'": use 'type(scope): description', ...
[amend -m 'merge: ...' on that merge] exit=1
    commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
[git merge -m lead landing-lane subject] exit=0
    subject: merge: origin/main into claude/x (lead, landing lane)
[conflicted merge resolved, commit -m lead subject] exit=0
    subject: merge: origin/main into claude/x (lead, landing lane) parents: 26aa115 f1299b7
[git revert --no-edit] exit=0
    subject: Revert "fix(grid): to revert"
[git revert of the revert] exit=0
    subject: Reapply "fix(grid): to revert"
```

## Tests and guard on the head

```text
$ python scripts/ci/test_pr_guard.py            # Python 3.15.2
Ran 45 tests in 8.004s
OK
$ python scripts/ci/pr_guard.py --base origin/main --head-ref lead/conventional-commits --head f0b35ea
pr_guard: code=True, 0 error(s)
exit=0
```

## The paths the brief asked about

| Path | Result at f0b35ea |
| --- | --- |
| In-flight PRs 7, 12, 13, 14, 16 and 18, after merging `main` | Pass with `<TASK-ID>: ` commits. Fail on their other pre-rule styles (**R3-1**). Their live titles (`A1: ...`, `B1: ...`, `EN-07: ...`) all match `LEGACY`. |
| The same PRs without merging `main` | The merge ref already runs the new guard once PR 19 lands, so this is the same as the row above. |
| Landing-lane merges | Pass. Merge commits are skipped by `--no-merges` in CI and by `MERGE_HEAD` in the hook, and the lead's `merge:` subject passes both. Amending a merge with the hook on fails (**R3-4**). |
| Integration batches | Pass for batched `<TASK-ID>: ` commits. Too permissive for new work (**R3-2**), and refuse other legacy styles (**R3-1**). |
| PRs stacked on legacy branches | PRs 12, 14 and 16 are themselves legacy, so they pass. A new stacked PR's range holds only its own commits, and still does after retargeting, because merges are `--merge`. It is linted only after retargeting (**R3-5**). |
| Docs-only PRs | Subject to the same commit and title checks. No path skips them, and no lead script writes fixed commit subjects or PR titles. |
| Pushes to `main` | There is no `base`, so neither commits nor titles are checked. Pass. |
| Reruns | `number` and the base SHA come from the replayed event, and the title is read live. A retitle followed by a rerun clears a title error. Pass. |
| Revert PRs | A `Revert "..."` or `Reapply "..."` title and commit pass, including a revert of a legacy-titled PR. |

## What re-review needs

- A fix for R3-1, with a test that fails before the fix and uses a non-`<TASK-ID>:` pre-rule subject such as `test(EN-07): ...`.
- R3-2 to R3-5 at the author's discretion. None of them blocks.
- A new fresh reviewer at the new head.
