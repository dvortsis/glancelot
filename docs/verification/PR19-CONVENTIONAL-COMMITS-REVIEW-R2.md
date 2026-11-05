# PR 19 review, round 2: Conventional Commits and SemVer

**Verdict: CHANGES REQUIRED**

- PR: 19, `ci(guard): enforce Conventional Commits and record the release policy`, branch `lead/conventional-commits`
- Reviewed head: `e3f2ba16c2fc7f2148db977d60e97bf89ee396b8`. The previous review (`PR19-CONVENTIONAL-COMMITS-REVIEW.md`) was at `c405156`.
- Diffs reviewed: `git diff c405156 e3f2ba1`, which has 6 files and 37 insertions and 14 deletions, and the whole PR, `git diff origin/main...e3f2ba1`, where `origin/main` is `20f1210` (10 files, 317 insertions, 12 deletions).
- Reviewer: a fresh-context Opus subagent that wrote none of this work and neither earlier review. It used a detached worktree at `.worktrees/review-pr19-r2`. The hook runs used a throwaway repository, and the PR simulations used a throwaway clone, both under the session scratchpad. The configuration of `C:\src\Github\Glancelot` was not changed.

All five round-1 findings are fixed as written, and fail-before evidence exists for each. Reading the whole PR afresh turned up a gap that the round-1 fix leaves open. The PR-number exemption covers a legacy PR only when that PR is merged by itself. The lead's normal integration path, `lead/integrate-<n>`, opens a new PR numbered 19 or higher. Any post-cutoff `<TASK-ID>: ` commit in a batched legacy packet turns that batch's `guard` red. A stacked PR cut from a legacy tip has the same problem. The fix is small, but it blocks, by the same bar round 1 applied to its finding 2.

## Findings, by severity

### N1. High (blocking): integration batches and stacked PRs that carry a legacy commit fail `guard`

`is_legacy_pr(event.get("number"))` exempts commits only in the PR whose own number is 18 or below. AGENTS.md ("Keeping PRs from colliding") makes batching the normal route: "The lead batches 2-4 approved packets into `lead/integrate-<n>` ... opens a PR". That PR is numbered 19 or higher, and its range `merge_base..head` contains every packet commit not yet on `main`. So, after November 6:

- A review-fix commit `EN-07: fix review finding` on PR 18 passes PR 18's own `guard`.
- The same commit fails `guard` on any `lead/integrate-<n>` PR that batches PR 18.
- It also fails `guard` on any new stacked packet (PR 19 or higher) cut from PR 18's tip before PR 18 merges.

Simulated in a scratch clone:
1. Simulated `main` after PR 19, `8c5ca8f`: `20f1210` plus a GitHub-style merge of `e3f2ba1`.
2. PR 18's branch `d228f43` merges that `main`, as `5d06d30`.
3. It then adds `EN-07: fix review finding`, committed 2026-11-07T10:00Z, at `d5a3920`.

```text
-- PR 18 directly                                  (event number 18)
pr_guard: code=True, 0 error(s)
exit=0
== lead batches PR 18 into lead/integrate-5 (a new PR, number 25), tip 5d82cd0
-- lead/integrate-5 as PR 25
::error::commit d5a392002: 'EN-07: fix review finding': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
pr_guard: code=True, 1 error(s)
exit=1
-- stacked PR 26 on PR 18's tip, tip 06046c6
::error::commit d5a392002: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
```

This is not a dead end, because the landing lane still works: the lead merges `main` into PR 18 and lands PR 18 by itself. But nothing tells the lead that.
- The lead finds out from a red `ci-ok` on a batch, possibly after a gate.
- `ci-known-red` cannot excuse a `guard` failure.
- Every legacy packet carrying such a commit then needs its own serial full gate.

This can affect all six open legacy PRs (7, 12, 13, 14, 16, 18). Their cloud sessions follow the AGENTS.md on their own branch and will keep writing `<TASK-ID>: ` subjects. The B1 stack (13, 14, 16) is exactly where stacked follow-ups are likely.

Fix, any of these:
- (a) In `pr_guard.main`, `legacy=is_legacy_pr(number) or is_integration_branch(head_ref)`. This mirrors the existing `wait_errors` exemption for integration branches, on the same grounds: the packet PR's own `guard` already checked those commits. It does not cover a stacked PR, so pair it with (b) for that case.
- (b) State in AGENTS.md ("Keeping PRs from colliding") and RELEASING.md two rules:
  - A legacy PR (18 or below) with any `<TASK-ID>: ` commit dated November 6 or later lands alone, through the landing lane, not in a batch.
  - No new PR is stacked on a legacy branch until that branch merges.
- (c) Exempt, in any PR, a legacy-format commit reachable from the head of an open PR numbered 18 or below. This is exact but needs a live `gh` lookup, the way `PR_LABELS` is read now.

(a) plus (b) for stacking is the smallest change. Include a test for whichever is chosen. For (a), a `pr_guard` run with an event file on a `lead/integrate-*` head that has a post-cutoff `A1:` commit should return 0 errors, and the same run on a `claude/*` head should return 1.

### N2. Medium (fold into N1's fix): the legacy wiring is untested

The round-1 fix for finding 1 has three links:
- `message_errors(legacy=)`;
- `range_errors` forwarding `legacy`;
- `main()` passing `is_legacy_pr(number)`.

Only the first link and `is_legacy_pr` are unit-tested. I mutated scratch copies of `scripts/ci`, one change at a time:

```text
== legacy: drop LEGACY clause in message_errors
FAIL: test_legacy_subjects_only_in_old_prs (__main__.CommitExemptions.test_legacy_subjects_only_in_old_prs)
FAILED (failures=1)
== legacy: is_legacy_pr always False
FAIL: test_legacy_subjects_only_in_old_prs (__main__.CommitExemptions.test_legacy_subjects_only_in_old_prs)
FAIL: test_old_prs_keep_task_titles (__main__.Titles.test_old_prs_keep_task_titles)
FAILED (failures=2)
== legacy: main() stops passing legacy to range_errors
Ran 43 tests in 5.542s
OK
== legacy: range_errors ignores its legacy arg
Ran 43 tests in 4.979s
OK
```

Two of the four mutations survive. That is the code path that keeps PRs 7 to 18 mergeable. The end-to-end simulation under N1 and finding 1 below shows the wiring works today, but nothing stops a regression.

Fix: add a `CommitRange` case in which a post-cutoff `A1: ...` commit gives 0 errors from `range_errors(..., legacy=True)` and 1 error with `legacy=False`. That kills the `range_errors` mutant. The `main()` mutant needs an event-file test. N1's test can cover both, if it runs `pr_guard.main()` with an event file for PR 18, where the commit passes, and for PR 19, where it fails.

### N3. Low: the documented pre-push check is red on a legacy branch that CI accepts

AGENTS.md "Before pushing" says to run `python scripts/ci/pr_guard.py --base origin/main --head-ref <branch>`. Locally there is no event file, so the number is `None` and `legacy` is off:

```text
== local pre-push run on PR 18 (no event file)
::error::commit d5a392002: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
```

A PR 18 session that merges `main` and follows "Do not push red" is left with a red it cannot clear without rewording pushed history. Fix: add a `--pr <n>` option to set `number`, which `main()` uses when there is no event, and mention it in AGENTS.md. Or add one line saying legacy PRs may ignore this specific error locally.

### N4. Low: a PR title from GitHub's Revert button fails, although RELEASING.md says it is accepted

RELEASING.md says: "Git's and GitHub's own `Revert "<subject>"` is accepted". For commits it is accepted. For PR titles it is not, because `title_errors` calls `subject_errors` directly and never reaches the `REVERT` exemption:

```text
PR title Revert: ['PR title: \'Revert "feat(grid): incremental listing (A1)"\': use \'type(scope): description\', ...']
commit Revert: []
commit Revert of a merge: []
```

It is easy to recover from, because retitling the PR is enough. Either accept `REVERT` in `title_errors`, or say in RELEASING.md that a revert PR is retitled `revert(scope): ...`.

### N5. Low: the hook still accepts a non-merge `Merge ...` subject

`LOCAL_ONLY` still begins with `"Merge "`, so the hook passes `git commit -m "Merge the two caches"`. RELEASING.md says that subject "is not exempt", and `guard` does refuse it.

```text
[Merge the two caches (no merge)] exit=0
```

With the `MERGE_HEAD` check in the hook, the prefix matters only for `git commit --amend` on a merge commit, which a range check skips anyway. This is not a correctness problem, because `guard` catches the commit. It is a weaker early warning than the docs suggest. Either drop `"Merge "` from `LOCAL_ONLY`, or leave it and say the hook allows it for amended merges.

### N6. Informational

- `Reapply "<subject>"`, which git 2.43 and later write when reverting a revert, is refused: `commit Reapply: ['\'Reapply "feat(grid): x"\': use ...']`. It is rare, and rewording before pushing is fine.
- A legacy match returns before the blank-line check, and the pattern also matches `LEAD: ...`. That matches the old title rule and only applies to PRs 18 and below, so it is acceptable.
- The hook has no automated test. The runs below are the evidence for finding 2.

## Round-1 findings: resolution

| # | Round-1 finding | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Legacy PRs permanently red after one old-format commit | **Fixed for the PR itself** (see N1 and N2 for the gaps) | PR 18 simulation below |
| 2 | Hook rejects the lead's `merge:` subject | **Fixed** | hook runs below |
| 3 | `Merge ` loophole in CI | **Fixed** | `Merge the two caches` now refused by `message_errors` (test `test_merges_and_local_fixups`) |
| 4 | `Revert "..."` refused | **Fixed for commits** (titles: N4) | `test_git_revert_subject_passes` |
| 5 | Docs claimed the hook was on | **Fixed** | AGENTS.md and RELEASING.md now say opt-in |

Commands and output for finding 1, on the same simulated PR 18 head `d5a3920`, with a CI-style event file:

```text
== fail-before: c405156's guard on the same PR 18 head
-- PR 18 under c405156
::error::commit d5a392002: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
-- PR 18, legacy commit after cutoff        (e3f2ba1)
pr_guard: code=True, 0 error(s)
exit=0
-- same commits as PR 19
::error::commit d5a392002: 'EN-07: fix review finding': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
== PR 18 still refuses other formats
-- PR 18 + 'update stuff'
::error::commit 63bba5362: 'update stuff': use 'type(scope): description', ...
pr_guard: code=True, 1 error(s)
exit=1
```

Commands and output for finding 2, from a throwaway repo with a repo-local `core.hooksPath scripts/hooks` and the PR's `commit_lint.py` and `commit-msg`:

```text
== fail-before: hook without the MERGE_HEAD line (git -c core.hooksPath=<copy>)
commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
Not committing merge; use 'git commit' to complete the merge.
exit=1
== current hook
Merge made by the 'ort' strategy.
exit=0
HEAD: merge: origin/main into claude/x (lead, landing lane) parents: a0b7522 282e6a0
[default merge subject] exit=0
    subject: Merge branch 'side'
== conflicted merge, resolved, then git commit -m lead-style
[commit after conflict] exit=0
    subject: merge: origin/main into claude/x (lead, landing lane) parents: dcd4440 1e963f2
```

Fail-before for findings 3 and 4, on scratch copies of `scripts/ci`:

```text
== merge: Merge prefix exempt everywhere (finding 3 reverted)
FAIL: test_merges_and_local_fixups (__main__.Commits.test_merges_and_local_fixups)
FAILED (failures=1)
== revert: drop REVERT
FAIL: test_git_revert_subject_passes (__main__.CommitExemptions.test_git_revert_subject_passes)
FAILED (failures=1)
== restored (pristine copy)
OK
```

## Checks that passed

### Tests and guard on the head

```text
$ python scripts/ci/test_pr_guard.py            # Python 3.15.2
Ran 43 tests in 6.401s
OK
$ python scripts/ci/pr_guard.py --base origin/main --head-ref lead/conventional-commits --head e3f2ba1
pr_guard: code=True, 0 error(s)
exit=0
```

### Hook (`scripts/hooks/commit-msg` at `e3f2ba1`)

```text
== bad subject
[A1: bad subject] exit=1
    commit_lint: 'A1: bad subject': use 'type(scope): description', ...
[update stuff] exit=1
    commit_lint: 'update stuff': use 'type(scope): description', ...
[fix(grid): good subject] exit=0
== Revert
[git revert --no-edit HEAD] exit=0
    subject: Revert "Merge the two caches"
[commit -m Revert "fix(grid): x"] exit=0
== linked worktree (relative hooksPath) on main
[wt bad] exit=1
    commit_lint: 'A1: bad in worktree': use 'type(scope): description', ...
[wt good] exit=0
[wt lead-style merge] exit=0
    subject: merge: origin/main into claude/x (lead, landing lane) parents: dcd4440 073ca7f
== linked worktree on a branch without the linter
    ls: cannot access 'scripts': No such file or directory
[old branch, relative hooksPath, bad subject] exit=0
[old branch, absolute hooksPath, bad subject] exit=0
```

`MERGE_HEAD` resolves per worktree, so the merge exemption works in a linked worktree. On a branch from before the rule, the hook either does not exist (relative `hooksPath`) or finds no `commit_lint.py` and exits 0 (absolute `hooksPath`).

### Whole-PR reread

- **`commit_lint.py`:**
  - `LOCAL_ONLY` applies only with `local=True`, which only `--file` (the hook) sets.
  - `REVERT` and `LEGACY` are anchored.
  - `range_errors` still uses `--no-merges` and the UTC cutoff.
  - Round 1's time-zone boundary checks are unaffected.
- **`pr_guard.py`:**
  - `title_errors` and the commit range now share one `LEGACY` pattern and `is_legacy_pr`.
  - A push to `main` (no `base`) checks neither.
  - The `guard` job reads the title live, and `number` comes from the event, which a rerun replays unchanged.
- **Docs:**
  - AGENTS.md, RELEASING.md, the decision record, the PR template and the REL-01 queue row agree with the code, apart from N1 (the batch path is not mentioned), N3 and N4.
  - No other file still requires `<TASK-ID>: ` or `LEAD: ` titles (`grep` over AGENTS.md, `scripts/`, `.github/`, `docs/CI-GATE-RUNNER.md` and `BUILDING.md`).
  - The lead scripts do not parse titles.
- **Round 1's "Checks that passed"** (`.gitattributes` LF for hooks, the Python probe, the release policy against the semantic-versioning skill, the scopes) are unchanged by `e3f2ba1`, so they were not repeated.

## What re-review needs

- A fix for N1, with a test that fails before the fix, and with N2's wiring tests.
- Fixes for N3 to N5, or a reason for leaving each one.
- A new fresh reviewer at the new head.
