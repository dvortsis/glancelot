# PR 19 review: Conventional Commits and SemVer

**Verdict: CHANGES REQUIRED**

- PR: 19, `ci(guard): enforce Conventional Commits and record the release policy`, branch `lead/conventional-commits`
- Reviewed head: `c405156a69970c51e5ce2dfe628e28e4533621f0`
- Diff reviewed: `git diff origin/main...c405156`. `origin/main` was `20f1210`. There are 10 files, with 294 insertions and 12 deletions.
- Reviewer: a fresh-context Opus subagent that implemented none of this work. It used a detached worktree at `.worktrees/review-pr19`, plus scratch clones in the session scratchpad for the simulations. No C++ build was needed.

Two findings block. Both are small fixes.
- Finding 1: the commit rule can leave an in-flight PR unmergeable with no legal way to fix it.
- Finding 2: the hook rejects the lead's own landing-lane merge subject.

Everything else checked out:
- the linter itself;
- the title rule;
- the cutoff arithmetic across time zones;
- the push-to-main path;
- the hook's Python probe and its behaviour in linked worktrees;
- the documents.

## Findings, by severity

### 1. High (blocking): one old-format commit pushed after the cutoff makes an in-flight PR permanently red

The title rule exempts PRs 18 and below (`LEGACY_TITLE_LAST_PR`). The commit rule does not: `range_errors` checks every non-merge commit committed from 2026-11-06T00:00Z, whatever the PR number. PRs 7, 12, 13, 14, 16 and 18 are open. Their sessions follow the AGENTS.md on their own branch, which says `<TASK-ID>: <title>` until they merge `main`. Cloud sessions cannot be messaged. A review-fix commit such as `EN-07: fix review finding`, pushed on or after November 6, turns `guard` red.

Once that happens, nothing can fix it:
- `ci-ok` fails outright on any `guard` failure (`if [ "$GUARD" != success ]; then ... exit 1`).
- `ci-known-red` only excuses `core` and `viewer` failures, and using it here would misuse it.
- A later commit cannot fix an earlier one.
- Rewording the commit means rewriting a pushed shared branch, which AGENTS.md forbids.

Reproduced in a scratch clone:
1. Merge `upstream/main` into `c405156` to simulate `main` after PR 19.
2. Merge that into `claude/en-07` (PR 18).
3. Add one commit with `GIT_COMMITTER_DATE=2026-11-06T12:00:00Z`.

```text
== in-flight PR 18 after merging main, one old-format commit on Nov 6:
::error::commit 28ae8d38b: 'EN-07: fix review finding': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
pr_guard: code=True, 1 error(s)
```

Any one of these fixes it, in order of preference:
1. Extend the PR-number exemption to the commit range. When `event.get("number") <= LEGACY_TITLE_LAST_PR`, skip `range_errors`, or accept the legacy `^[A-Z][A-Z0-9-]*: \S` subject alongside Conventional ones. This keeps the rule strict for every new PR.
2. Add a lead-only label, such as `legacy-commits`, that `pr_guard` reads live (as `PR_LABELS` already is) to skip the commit check.
3. Document a sanctioned, owner-only `git push --force-with-lease` for this case. This is the weakest option, because it conflicts with the no-force-push rule.

Today, before the cutoff, every in-flight branch passes, so this is a trap for the future, not a failure now. The evidence is under "In-flight PRs" below.

### 2. Medium (blocking): the `commit-msg` hook rejects the lead's landing-lane merge subject

The lead's routine `main` merge uses the subject `merge: origin/main into <branch> (lead, landing lane)`. It appears four times in history: `git log --format=%s --all | grep "^merge: "` lists the merges into `b3-export-ui`, `stack-confirm-fix` and `phase2-contracts`.
- **In CI it is fine.** `range_errors` runs `git log --no-merges`, so merge commits are never linted.
- **The hook is where it fails.** `git merge` runs `commit-msg`, and `message_errors` exempts only subjects that start with `Merge ` (capital M, then a space). It rejects the merge.

A real temporary repo with `core.hooksPath=scripts/hooks` shows the rejection:

```text
== merges
commit_lint: 'merge: origin/main into claude/x (lead, landing lane)': type 'merge' is not one of feat, fix, perf, refactor, test, docs, build, ci, chore, revert
Not committing merge; use 'git commit' to complete the merge.
lead-style merge exit=1
default merge exit=0 subject=Merge branch 'side'
```

AGENTS.md says the hook "is on for the shared clone on this PC", so the lead would hit this on its next landing-lane merge. `--no-verify` works around it, but a documented lead routine should not need it. RELEASING.md says merge commits "keep git's own subject", which quietly retires the `merge:` subject, but AGENTS.md doesn't say so.

Fix: in `scripts/hooks/commit-msg`, exit 0 when a merge is in progress, before running the linter:

```sh
git rev-parse -q --verify MERGE_HEAD >/dev/null && exit 0
```

This covers any merge subject without widening `commit_lint`. Alternatively, say in AGENTS.md that landing-lane merges now use git's default subject.

### 3. Low: the `Merge ` prefix exemption is a loophole on the CI path

`message_errors` exempts any subject that starts with `Merge `. In `range_errors`, real merges are already removed by `--no-merges`, so there the exemption only ever lets a single-parent commit through. For example, `Merge the two thumbnail caches` passes `guard`. Pass a flag from `range_errors` so the prefix exemption applies only in the hook. With the `MERGE_HEAD` check from finding 2, the hook doesn't need it either.

### 4. Low: `Revert "..."` subjects fail

Git's default revert subject, and the title and commit that GitHub's Revert button produces, are `Revert "<subject>"`:

```text
['\'Revert "feat(io): add f"\': use \'type(scope): description\', ...']   # message_errors('Revert "feat(io): add f"\n\nThis reverts commit abc.')
[]                                                                       # message_errors('revert(io): add f\n\nThis reverts commit abc.')
```

`git revert --no-edit` did not run the hook in the test repo, so the commit was created and only `guard` would catch it. Either exempt `^Revert "` (commitlint's default ignores do the same), or add one line to RELEASING.md: reword git's revert subject to `revert(scope): ...` before pushing.

### 5. Low (doc accuracy): the hook is not on for the shared clone

AGENTS.md says the hook "is on for the shared clone on this PC", and RELEASING.md says it "is enabled for the shared clone on the lead's PC". Neither is true yet:

```text
$ git -C C:/src/Github/Glancelot config --show-origin --get-all core.hooksPath
(no output)
$ ls .git/hooks | grep -v sample
(no output)
```

`.git/hooks` holds no real hooks, so setting `core.hooksPath scripts/hooks` shadows nothing. Either the lead sets it when PR 19 merges, which should come after finding 2's fix, or the sentences are reworded to "enable with ...".

### 6. Informational: the committer-date cutoff is best effort, and that is acceptable

- A session can backdate `GIT_COMMITTER_DATE` and slip past the rule.
- In the other direction, `rebase`, `amend` or `cherry-pick` of a pre-rule commit after the cutoff gives it a new committer date, so it is flagged. The fix is to reword it, which is fine on a branch that hasn't been pushed.

This is acceptable for three reasons. The hook checks regardless of date. Reviewers read every subject. Rewriting shared branches is already forbidden.

The boundary itself is exact across time zones:
- `16:59:59-07:00` and `09:59:59+10:00`, both `23:59:59Z`, are exempt.
- `17:00:00-07:00` and `13:30:00+13:30`, both `00:00:00Z`, are flagged.

```text
2026-11-06T13:30:00+13:30 A1: 06T13:30+13:30 (=06T00:00Z)
2026-11-06T09:59:59+10:00 A1: 06T09:59:59+10:00 (=05T23:59:59Z)
2026-11-05T17:00:00-07:00 A1: 17:00:00-07:00 (=06T00:00Z)
2026-11-05T16:59:59-07:00 A1: 16:59:59-07:00 (=05T23:59:59Z)
FLAGGED commit 6092d848c: 'A1: 06T13:30+13:30 (=06T00:00Z)': ...
FLAGGED commit 4adddc244: 'A1: 17:00:00-07:00 (=06T00:00Z)': ...
```

The same check under `py -3.12`, which is 3.12.10 and matches ubuntu-24.04's interpreter, printed `3.12.10 2 flagged`. The PR's own `guard` job on ubuntu-24.04 ran `test_pr_guard.py` and `pr_guard.py`, including the `CommitRange` test, and passed: https://github.com/dvortsis/glancelot/actions/runs/42088156233/job/127458818290.

## Checks that passed

### Tests and guard on the head

```text
$ python scripts/ci/test_pr_guard.py          # Python 3.15.2, PyYAML 6.0.2
Ran 41 tests in 2.692s
OK
$ python scripts/ci/pr_guard.py --base origin/main --head-ref lead/conventional-commits --head c405156
pr_guard: code=True, 0 error(s)
```

`docs/WORK-QUEUE.yaml`, with the new REL-01 row, is valid.

### Fail-before

I stubbed `subject_errors` to `return []` in a scratch clone, ran the tests, then restored it:

```text
FAIL: test_only_commits_after_the_cutoff_count (__main__.CommitRange.test_only_commits_after_the_cutoff_count)
FAIL: test_other_subjects_fail (__main__.Commits.test_other_subjects_fail)
FAIL: test_old_prs_keep_task_titles (__main__.Titles.test_old_prs_keep_task_titles)
FAIL: test_other_titles_fail (__main__.Titles.test_other_titles_fail)
Ran 41 tests in 2.920s
FAILED (failures=4)
restored
OK
```

I also replaced the cutoff comparison with `if False:`, so no commit is exempt:

```text
FAIL: test_only_commits_after_the_cutoff_count (__main__.CommitRange.test_only_commits_after_the_cutoff_count)
FAILED (failures=1)
restored
```

### Titles

The six in-flight titles pass with their numbers. A legacy title fails from PR 19 on:

```text
7 'A1: incremental cancellable folder listing' []
12 'A1: Show more for folders beyond the first 100,000 entries' []
13 'B1: asset grouping and expansion' []
14 'B1: scoped plans in the delete, transfer and rename services' []
16 'B1: companions in the delete, copy, move and rename confirmations' []
18 'EN-07: per-component command handling' []
19 'ci(guard): enforce Conventional Commits and record the release policy' []
19 'EN-07: x' ["PR title: 'EN-07: x': use 'type(scope): description', ..."]
20 'LEAD: integrate 3' ["PR title: 'LEAD: integrate 3': type 'LEAD' is not one of feat, fix, ..."]
```

### In-flight PRs today

For each in-flight branch, I merged a simulated post-PR-19 `main` (a merge commit, which is excluded) and ran `pr_guard`. The only errors are the existing size errors. Those also appear against today's `main` without PR 19, and come from running locally without the PR body's `Large PR:` line:

```text
claude/a1-perf-enum non-merge commits=12 -> pr_guard: code=True, 0 error(s)
claude/a1-load-more non-merge commits=21 -> pr_guard: code=True, 1 error(s)   # 3219 changed lines (size, pre-existing)
claude/b1-asset-group non-merge commits=2 -> pr_guard: code=True, 0 error(s)
claude/b1-asset-services non-merge commits=3 -> pr_guard: code=True, 0 error(s)
claude/b1-asset-ui non-merge commits=6 -> pr_guard: code=True, 1 error(s)    # 3375 changed lines (size, pre-existing)
claude/en-07 non-merge commits=4 -> pr_guard: code=True, 0 error(s)
```

Only the cutoff exempts these old commits:

```text
a1-perf-enum cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 12
a1-load-more cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 21
b1-asset-group cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 0
b1-asset-services cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 0
b1-asset-ui cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 0
en-07 cutoff=2026-11-06: 0  cutoff=2020 (exemption removed): 4
```

### Push to `main`

With an event file that has no `pull_request` key, `base` is empty, so neither the commit range nor the title is checked:

```text
$ GITHUB_EVENT_PATH=push_event.json python scripts/ci/pr_guard.py
pr_guard: code=True, 0 error(s)
```

### Message shapes

- `perf(decode)!: drop the copy` passes, as does a body with `Task: A1` and `Co-Authored-By:` trailers. In the real repo, `feat(ui)!: break it` with those trailers committed with exit 0.
- A `BREAKING CHANGE:` footer passes.
- These fail: `fixup!` in CI, a capitalised type, a capitalised scope, a missing space, a trailing full stop, a subject over 100 characters, and a missing blank line after the subject.

### Hook (`scripts/hooks/commit-msg`)

- **Index entry:** the file is LF in both the index and the worktree, and is executable:

  ```text
  100755 6d8efd29c59f425b2e4e992b64a5973fe9fdb14b 0	scripts/hooks/commit-msg
  i/lf    w/lf    attr/text eol=lf      	scripts/hooks/commit-msg
  ```

- **Python probe in Git Bash:** `python3` is the Store stub. It prints "Python was not found; ..." and exits 49, and the probe discards that output and moves on to `python`:

  ```text
  python3: /c/Users/dvort/AppData/Local/Microsoft/WindowsApps/python3
    probe exit=49
  python: /c/Python315/python
    probe exit=0
  ```

- **Real temporary repo:** with an absolute `git -c core.hooksPath=<path> commit` and with a relative `core.hooksPath scripts/hooks`:

  ```text
  == absolute hooksPath via -c
  commit_lint: 'A1: bad subject': use 'type(scope): description', ...
  exit=1
  exit=0                       # fix(grid): good subject
  == relative hooksPath
  commit_lint: 'update stuff': use 'type(scope): description', ...
  exit=1
  exit=0                       # feat(ui)!: break it + Task/Co-Authored-By trailers
  fixup exit=0
  ```

- **Linked worktrees:** a relative `hooksPath` resolves against each worktree's root.
  - A worktree on a pre-rule branch has no `scripts/hooks`, so git runs no hook.
  - With an absolute `hooksPath` the hook does run there, hits the missing `commit_lint.py`, and exits 0.
  - A worktree on a post-rule branch enforces the rule.

  ```text
  old-branch worktree (no scripts/hooks) exit=0
  new worktree bad exit=1
  new worktree good exit=0
  == hook from old-branch worktree with absolute hooksPath (lint missing):
  exit=0
  ```

### Docs

- **`docs/RELEASING.md` against the semantic-versioning skill:** it agrees on every point the brief lists:
  - Conventional Commits with the skill's ten types;
  - `!` or a `BREAKING CHANGE:` footer, with a sensible definition of breaking for a desktop app;
  - SemVer 2.0.0, with the 0.y.z rule stated explicitly (breaking or `feat` raises MINOR, `fix` or `perf` raises PATCH, and 1.0.0 is the user's call);
  - one version source in `project(Glancelot VERSION 0.1.0)`, with nothing hand-written elsewhere and no SHA in source;
  - immutable annotated `vX.Y.Z` tags, and a bad release fixed with a new patch;
  - `-rc.N` and `-dev.N` pre-releases, promoting only a gated commit;
  - provenance: version, full and short SHA, UTC build time taken once, and build kind, with a release failing without a SHA;
  - Docker `X.Y.Z` and `sha-<full SHA>` tags, with the OCI `version`, `revision` and `created` labels;
  - `latest` only as a convenience alias.
- **Two reasonable adaptations:**
  - "Build kind" stands in for the skill's "deployment environment".
  - `GLANCELOT_VERSION_SUFFIX` is a configure-time input, not a second version source.
- **Consistency with AGENTS.md:** only the lead prepares a release, and only after the user approves it. No network and no telemetry are involved. History is not rewritten. Merges go through the normal gate.
- **Scopes:** the list fits the tree. `src/render` maps to `render` and `canvas`, `src/worker` (the decode worker process) to `decode`, `src/core` to `fs`, `io`, `ops`, `export` and the other core scopes, and `ui/` to `ui`, `theme` and the surface scopes. Scopes name subsystems rather than directories, as the skill asks.
- **AGENTS.md's changed lines:** they agree with the code (the cutoff date, PR 19 and later, and `docs(queue)` and `docs(evidence)` for bookkeeping). Three exceptions: finding 5's "is on" claim, finding 2's undocumented retirement of the `merge:` subject, and finding 1's gap for PRs 18 and below.
- **PR template:** consistent.
- **Decision record:** consistent.

## What re-review needs

- Fixes for findings 1 and 2, each with a test or a hook run that fails before the fix.
- Fixes for findings 3 to 5, or reasons for leaving them.
- A new fresh reviewer at the new head.
