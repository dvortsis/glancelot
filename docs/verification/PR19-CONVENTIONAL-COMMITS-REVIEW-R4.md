# PR 19 review, round 4: Conventional Commits and SemVer

**Verdict: APPROVED**

- **PR:** 19, `ci(guard): enforce Conventional Commits and record the release policy`, on branch `lead/conventional-commits`.
- **Reviewed head:** `1395a4d405f9de52f763b5efa5b57ff7f563f3c7`. Earlier rounds reviewed `c405156` (round 1, `PR19-CONVENTIONAL-COMMITS-REVIEW.md`), `e3f2ba1` (round 2, `-R2.md`) and `f0b35ea` (round 3, `-R3.md`).
- **Diffs reviewed:**
  - `git diff 1395a4d~1 1395a4d`: 5 files, 43 insertions, 21 deletions (`docs/RELEASING.md`, `scripts/ci/commit_lint.py`, `scripts/ci/pr_guard.py`, `scripts/ci/test_pr_guard.py`, `scripts/hooks/commit-msg`).
  - The whole PR, `git diff origin/main...1395a4d`, with `origin/main` at `20f1210`: 10 files, 376 insertions, 12 deletions.
- **Reviewer:** a fresh-context Opus subagent that wrote none of this work and none of the three earlier reviews.
- **Where the review ran:**
  - Code was read in a detached worktree, `.worktrees/review-pr19-r4`.
  - Mutations ran on a scratch copy of `scripts/ci`, restored from a backup copy after each one.
  - PR simulations ran in a throwaway clone under the session scratchpad. Every scratch commit was dated `2026-11-07T10:00:00Z`.
  - Hook runs used a throwaway repository with a repo-local `core.hooksPath scripts/hooks`.
  - The configuration of `C:\src\Github\Glancelot` was not changed. `git config --local --get core.hooksPath` there is still unset.

Round 3's blocking finding, R3-1, is fixed in the way R3 recommended. PRs numbered 18 or below, and `lead/integrate-<n>` batches, now skip the commit-subject check entirely. Every other PR's post-cutoff non-merge commits are still checked. Each new check has a test that catches its removal.

R3-3 is fixed. R3-4 is fixed for the case it reported (`--amend --no-edit` on a merge). R3-2 and R3-5 are documented in RELEASING.md.

Reading the whole PR afresh found nothing blocking:
- No legitimate path is left permanently red.
- A PR numbered 19 or above that is not an integration batch can't add old-format commits after the cutoff.

The notes below are polish.

## Round-3 findings: resolution

| # | Round-3 finding | Status | Evidence |
| --- | --- | --- | --- |
| R3-1 | Legacy PRs and batches red on other pre-rule styles | **Fixed** | `commit_errors` returns `[]` when `is_legacy_pr(number) or is_integration_branch(head_ref)`. The test uses `EN-07: ...`, `test(EN-07): ...` and `wip: x`. It fails before the fix, the mutations are caught, and the PR 18 simulation passes. |
| R3-2 | Batches accept every legacy-form commit | **Documented** in RELEASING.md: "the lead batches only PRs whose own `guard` passed". See N2. | - |
| R3-3 | CI's PR-number path untested | **Fixed** | `test_ci_reads_the_pr_number_from_the_event` writes a CI-style event file and passes no `--pr`. The mutation `number = args.pr` fails it. |
| R3-4 | Hook blocks amending a merge | **Fixed for `--amend --no-edit`** (round 3 saw `exit=1` at `f0b35ea`; now `exit=0`). Amending a merge to the lead's `merge: ...` subject is still refused. See N3. | Hook runs below |
| R3-5 | A stacked new PR is linted only after retargeting | **Documented** in RELEASING.md. The instruction as written can't run on such a branch; see N1. | - |

### Fail-before: the new tests against the previous code

This run uses `1395a4d`'s `test_pr_guard.py` with `1395a4d~1`'s `pr_guard.py` and `commit_lint.py`:

```text
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
Ran 46 tests in 8.824s
FAILED (failures=2)
```

### Mutations (one at a time on a scratch copy, then restored)

```text
== R3-1: batches checked again (drop is_integration_branch)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
FAILED (failures=1)
== R3-1: old PRs checked again (drop is_legacy_pr)
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
FAILED (failures=3)
== R3-1: commit check removed for everyone
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
FAILED (failures=3)
== R3-1: legacy boundary off by one (< 18)
FAIL: test_legacy_prs (__main__.CommitExemptions.test_legacy_prs)
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
FAIL: test_old_prs_keep_task_titles (__main__.Titles.test_old_prs_keep_task_titles)
FAILED (failures=5)
== R3-1: legacy boundary off by one (<= 19)
FAIL: test_legacy_prs (__main__.CommitExemptions.test_legacy_prs)
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAIL: test_old_prs_and_integration_batches_skip_the_subject_check (__main__.CommitRange.test_old_prs_and_integration_batches_skip_the_subject_check)
FAIL: test_old_prs_keep_task_titles (__main__.Titles.test_old_prs_keep_task_titles)
FAILED (failures=5)
== R3-3: CI path ignores the event number
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAILED (failures=1)
== R3-3: --pr ignored
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAILED (failures=1)
== main() checks commits only with an event
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAILED (failures=1)
== main() drops commit_errors
FAIL: test_ci_reads_the_pr_number_from_the_event (__main__.CommitRange.test_ci_reads_the_pr_number_from_the_event)
FAIL: test_main_passes_the_pr_number_through (__main__.CommitRange.test_main_passes_the_pr_number_through)
FAILED (failures=2)
== cutoff ignored (all commits checked)
FAIL: test_only_commits_after_the_cutoff_count (__main__.CommitRange.test_only_commits_after_the_cutoff_count)
FAILED (failures=1)
== restored
Ran 46 tests in 7.254s
OK
pristine
```

`pristine` means `diff -r` found no difference between the restored scratch copy and the reviewed worktree's `scripts/ci`.

The hook's new `HEAD^2` branch has no automated test, because the hook isn't unit-tested. The hook runs below cover it.

## Findings, by severity

None blocks.

### N1. Low: R3-5's instruction for stacked PRs can't run as written

RELEASING.md says a new PR stacked on a pre-rule branch should "run `commit_lint.py --range` on it before pushing". Two things get in the way:
- That branch's checkout has no `scripts/ci/commit_lint.py`. None of the open legacy branches has it (round 3 checked).
- The documented range, `origin/main..HEAD`, would also flag the parent's post-cutoff legacy commits, which are exempt on the parent's own PR.

A session on such a branch also reads its base's pre-rule AGENTS.md, so it may never see the instruction.

Nothing goes permanently red. After the parent lands and the PR is retargeted, the range holds only the PR's own commits, because `lead-merge.ps1` merges with `--merge`. Commits written in the new format pass.

Suggested wording: "run `python <main checkout>/scripts/ci/commit_lint.py --range <base-branch>..HEAD`". Also put the commit format in the brief of any new packet stacked on a legacy branch.

### N2. Low: the integration-batch exemption is documented only in RELEASING.md

R3-2's mitigation ("the lead batches only PRs whose own `guard` passed") is in RELEASING.md. The batching procedure itself is in AGENTS.md, "Keeping PRs from colliding", which doesn't mention it.

Non-merge commits the lead makes directly on a `lead/integrate-<n>` branch, such as a hand-made fix after a conflict, are also unchecked. The batch PR's title is still checked, and so is the merge subject it becomes on `main`. Only the lead pushes `lead/` branches, so this is about the lead's discipline. A one-line AGENTS.md note would close it.

### N3. Low: with the hook on, amending a merge to the lead's own `merge: ...` subject is refused

```text
[amend -m 'merge: ...' on that merge] exit=1
    commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
```

The new `case` exempts only a first line that starts with `Merge `. The hook is opt-in and `guard` skips merge commits, so the workaround is `--no-verify` for that one amend. Making it `"Merge "*|"merge: "*)` would close it.

### N4. Low: the hook lets a non-merge `Merge ...` subject through when `HEAD` is a merge, and `guard` refuses it

This is the trade-off the brief names. It was reproduced here:

```text
merge: Merge branch 'side3' into gap parents=33d94c4 ce24817
hook exit=0
Merge the two caches parents=85454c9
commit_lint: commit 6cca47b9f: 'Merge the two caches': use 'type(scope): description', ...
commit_lint --range exit=1
```

The commit is still local. The required pre-push `pr_guard.py` run catches it, and amending before the push fixes it. The hook can't tell an amend from a new commit without the `prepare-commit-msg` source argument, so the current choice is reasonable.

### N5. Polish: the docs describe the legacy exemption three ways

- AGENTS.md: PRs 18 and earlier "may keep `<TASK-ID>: ` subjects".
- RELEASING.md: their commits "aren't checked ... though sessions use the new format there from now on".
- The code skips every commit subject on those PRs and on `lead/integrate-*` batches.

AGENTS.md doesn't mention the batch exemption. These don't conflict in effect, but one sentence in AGENTS.md matching RELEASING.md would remove the guesswork.

### N6. Informational: a bad subject pushed to a new PR has no sanctioned recovery

On a PR numbered 19 or higher, a pushed post-cutoff commit with a bad subject stays red. Rewording a pushed branch is forbidden, and `ci-known-red` doesn't excuse `guard`.

The required pre-push `pr_guard.py` run catches it first, so only a session that skips that check gets here. Two related routes, both rare:
- GitHub's "Apply suggestions from code review" button writes a non-Conventional commit. This workflow reviews through documents, not inline suggestions.
- Backdating the committer date (for example `rebase --committer-date-is-author-date`) gets past the cutoff, but that takes intent.

There is no `dependabot.yml`, and no lead script writes or parses PR titles (`grep -n -i title scripts/lead-*.ps1` exits 1 with no matches). One RELEASING.md line on what to do (a new branch with reworded commits, and a new PR) would help.

## Requested simulations

The throwaway clone has these branches:
- Simulated `main` after PR 19: `ba0b8f2`, which is `origin/main` (`20f1210`) plus a `--no-ff` merge of `1395a4d`.
- PR 18: `fab1e45`, which is PR 18's real tip `origin/claude/en-07` (`d228f43`) plus a commit `test(EN-07): fix review finding`, committed 2026-11-07.

Each run:
- checks out a merge-ref-style merge of the head into simulated `main`, so the guard is `1395a4d`'s;
- writes a CI-style `GITHUB_EVENT_PATH` file (`number`, `pull_request.number`, `base.sha`, `head.sha`, `head.ref`);
- passes no `--pr`.

```text
-- PR 18 + post-cutoff 'test(EN-07): fix review finding', main not merged in: PR 18, claude/en-07, title 'EN-07: per-component command handling', guard from merge ref 1395a4d
pr_guard: code=True, 0 error(s)
exit=0
-- PR 18 after landing-lane merge of main: PR 18, claude/en-07, title 'EN-07: per-component command handling', guard from merge ref 1395a4d
pr_guard: code=True, 0 error(s)
exit=0
-- PR 30 on claude/x with the same commit: PR 30, claude/x, title 'test(input): loupe order (EN-07)', guard from merge ref 1395a4d
::error::commit 15e9a8556: 'test(EN-07): fix review finding': scope 'EN-07' must be lowercase words joined by hyphens
pr_guard: code=False, 1 error(s)
exit=1
-- PR 30 on claude/x with a Conventional commit: PR 30, claude/x, title 'test(input): loupe order (EN-07)', guard from merge ref 1395a4d
pr_guard: code=False, 0 error(s)
exit=0
-- PR 30 on claude/x, Conventional commit, legacy title: PR 30, claude/x, title 'EN-07: loupe order', guard from merge ref 1395a4d
::error::PR title: 'EN-07: loupe order': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
pr_guard: code=False, 1 error(s)
exit=1
-- lead/integrate-30 batching the PR 18 head: PR 30, lead/integrate-30, title 'chore(integrate): batch 30 (EN-07)', guard from merge ref 1395a4d
pr_guard: code=True, 0 error(s)
exit=0
-- lead/integrate-30 batching the PR 18 head, legacy title: PR 30, lead/integrate-30, title 'LEAD: batch 30', guard from merge ref 1395a4d
::error::PR title: 'LEAD: batch 30': type 'LEAD' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
pr_guard: code=True, 1 error(s)
exit=1
```

All three required cases match the brief:
- PR 18 passes.
- PR 30 on `claude/x` fails on the commit.
- `lead/integrate-30` passes.

The batch's title is still checked. All open PRs today are 7, 12, 13, 14, 16 and 18, which are legacy, plus 19 (`gh pr list --state open`).

## Hook (`scripts/hooks/commit-msg` at 1395a4d, git 2.56.0.windows.4)

```text
[A1: bad subject] exit=1
    commit_lint: 'A1: bad subject': use 'type(scope): description', ...
[update stuff] exit=1
    commit_lint: 'update stuff': use 'type(scope): description', ...
[test(EN-07): x] exit=1
    commit_lint: 'test(EN-07): x': scope 'EN-07' must be lowercase words joined by hyphens
[Merge the two caches (not a merge, HEAD not a merge)] exit=1
    commit_lint: 'Merge the two caches': use 'type(scope): description', ...
[fix(grid): good subject] exit=0
[fixup! fix(grid): good subject] exit=0
[amend a normal commit to 'Merge the caches' (HEAD^2 absent)] exit=1
    commit_lint: 'Merge the caches': use 'type(scope): description', ...
[git merge, default subject] exit=0
    subject: Merge branch 'side' parents: 4b2c56d f8585d7
[amend --no-edit on that merge (no MERGE_HEAD)] exit=0
    parents: 4b2c56d f8585d7
[amend -m 'merge: ...' on that merge] exit=1
    commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
[non-merge 'Merge the two caches' right after a merge] exit=0
    subject: Merge the two caches parents: 8e6b8ab
[git merge -m lead landing-lane subject] exit=0
[git revert --no-edit] exit=0
    subject: Revert "fix(grid): to revert"
[git revert of the revert] exit=0
    subject: Reapply "fix(grid): to revert"
```

## Tests and guard on the head

```text
$ python scripts/ci/test_pr_guard.py            # Python 3.15.2
Ran 46 tests in 6.352s
OK
$ python scripts/ci/pr_guard.py --base origin/main --head-ref lead/conventional-commits --head 1395a4d
pr_guard: code=True, 0 error(s)
exit=0
```

## The paths the brief asked about

| Path | Result at 1395a4d |
| --- | --- |
| PRs 18 and below, with or without merging `main` | Commit subjects aren't checked. `<TASK-ID>: ` titles pass. Pass. |
| `lead/integrate-<n>` batches | Commit subjects aren't checked. The title must be Conventional (batch PR number is 19 or higher). Pass; see N2. |
| New PRs (19 and above, not a batch) | Every non-merge commit committed on or after 2026-11-06T00:00Z is checked, and the title must be Conventional. `test(EN-07): ...` and `EN-07: ...` are refused. |
| PRs with no number (local runs without `--pr`) | Checked like a new PR. `--pr <n>` gives an old PR's exemption locally. |
| Landing-lane merges | Merge commits are skipped by `--no-merges`. The hook skips them through `MERGE_HEAD` or, on amend, `HEAD^2` with a `Merge ` subject. Pass; see N3. |
| Stacked new PRs on legacy branches | Not linted until retargeted, then only their own commits are. Pass; see N1. |
| Revert PRs | `Revert "..."` and `Reapply "..."` titles and commits pass. |
| Pushes to `main` | There is no base, so nothing is checked. Pass. |

## What merge needs

- Nothing more from review. N1 to N6 are at the author's discretion.
- The `fresh-review` status is posted on `1395a4d405f9de52f763b5efa5b57ff7f563f3c7`. A docs or code push after this needs a new review status.
