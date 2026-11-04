# LEAD-MERGE-PROTECTIONS review

**Verdict: CHANGES REQUIRED**

Written for: the lead, who applies the fixes before PR 1 merges.

- Pull request: https://github.com/dvortsis/glancelot/pull/1
- Reviewed head: `b216dc0d401c67c3926b4ab1f9fc7d4f4ea66beb`, base `8a53196b4205f745c5ee536aedf8f1456b1878f7`
- Reviewer: fresh-context Opus session that wrote none of this.

Both blocking findings are in how `lead-merge.ps1` and `pr_guard.py` decide what a merge needs. Merges are not blocked for ordinary PRs, and no pull request can reach the self-hosted runner. A bad merge can get through in two ways.

## Blocking

### B1. A gated head can merge onto a newer `main`, so the tree that lands was never gated

`scripts/lead-merge.ps1:35`, against `AGENTS.md:16` ("on the exact commit to be merged") and `AGENTS.md:43` ("on the integrated commit").

The script checks `local-gate` on the PR head and then runs `gh pr merge --merge`. That creates a new merge commit. Nothing checks that the head contains the code now on `main`, and `ci-ok` is deliberately not required to be up to date (`docs/CI-GATE-RUNNER.md`). The old process did enforce this by merging `main` in first, under `integration_lock`. This PR removes that step from AGENTS.md.

Failure: packets A and B are both gated on `main` at X. A merges. `lead-merge.ps1 -Pr B` still passes, because B's `local-gate` is green on B's head. The merged A+B tree has had no gate, and with up to four child sessions this will happen. The hosted build on `main` catches a break only after it has landed, and it has no GPU.

Fix: for non-docs PRs, refuse when `main` has code the head lacks. Docs-only drift stays allowed, so a bookkeeping PR does not force a re-gate.

```powershell
if (-not $docsOnly) {
    $drift = @(gh api --paginate "repos/$Repo/compare/$sha...main" --jq '.files[].filename' | Where-Object { -not (Test-DocsOnly $_) })
    if ($drift.Count) { $problems.Add("main has code changes this head lacks ($($drift.Count) files): merge main in and re-gate.") }
}
```

`-Auto` has a small remaining window, because `main` can move while it waits for `ci-ok`. Accept that for docs-only PRs and refuse `-Auto` for code PRs.

### B2. The merge classification reads a truncated, rename-blind file list

`scripts/lead-merge.ps1:19,22,32-35` and `scripts/ci/pr_guard.py:196`.

- **Truncation.** `gh pr view --json files` asks for `files(first: 100)` with no paging (confirmed with `GH_DEBUG=api`, gh 2.103.3), and files come back in path order. `docs/` sorts before `include/`, `src/`, `tests/` and `ui/`. A `lead/` PR with 100 or more evidence files and code only in those directories is classified as bookkeeping, so it merges on `ci-ok` alone, with no review and no gate. Five of the last 40 merges on `main` had over 100 files (up to 1,854). All five happened to have a code file sorted ahead of `docs/`.
- **Renames.** `git diff --name-only` lists only the new path, and `PullRequestChangedFile` has no previous-path field (schema fields: `additions, changeType, deletions, path, viewerViewedState`). I ran the guard on local throwaway commits:

  ```text
  git mv CMakeLists.txt docs/CMakeLists.md                     -> pr_guard: code=False, 0 error(s), rc=0
  git mv src/app/command_registry.cpp docs/command_registry.cpp -> pr_guard: code=False, 0 error(s), rc=0
  ```

  `ci-ok` then passes with no builds. On a `lead/` branch, a move into `docs/handoff/` or `docs/verification/` also needs no review and no gate, and `main` no longer builds. The same gap lets a `claude/` branch move files out of `scripts/ci/` past `lead_only_errors`.

Fix:

- `pr_guard.py:196`: `git("diff", "--name-only", "--no-renames", merge_base, head)`. Leave the `-U0` diff at line 155 alone, so a moved test does not re-lint its old lines.
- `lead-merge.ps1`: build `$paths` from REST, include the old names, and fail if the count is short:

  ```powershell
  $pull = gh api "repos/$Repo/pulls/$Pr" | ConvertFrom-Json
  $files = @(gh api --paginate "repos/$Repo/pulls/$Pr/files" --jq '.[]|[.filename, (.previous_filename // "")]|@tsv')
  if ($files.Count -ne $pull.changed_files) { throw "PR $Pr lists $($files.Count) of $($pull.changed_files) files." }
  $paths = @($files | ForEach-Object { $_ -split "`t" } | Where-Object { $_ })
  ```

- Add both rename cases to `scripts/ci/test_pr_guard.py`, using a real temporary repository, since the current tests call the helpers directly.

## Non-blocking

| # | Where | Severity | Finding | Fix |
| --- | --- | --- | --- | --- |
| N1 | `build.yml:7,26`; `pr_guard.py:181-186,200,204` | Medium | `guard` reads labels, title and body from the event. `pull_request:` has no `types:`, so labelling or editing starts no run, and a rerun replays the old event (the problem the `ci-ok` comment already works around). Adding `lead-grant`, fixing a title or adding `Large PR:` has no effect until the next push or a close and reopen. `PR_LABELS` is read but never set. | In the `check` step, read them live with `gh pr view` into `PR_LABELS`, `PR_TITLE` and `PR_BODY` (needs `GH_TOKEN`). Have `pr_guard.py` prefer those over the event. |
| N2 | `build.yml:76` | Medium | `ci-known-red` stays on across pushes, so a new real failure on a later push passes `ci-ok`. It also accepts `cancelled`. `local-gate` limits the damage on code PRs. | Honour the label only when `github.run_attempt > 1` and each of `CORE`, `VIEWER` is `success` or `failure`. |
| N3 | `decisions/2026-11-04-merge-protections.md:24`; `build.yml:24-26` | Medium | "A session cannot relax its own check" is not true. On `pull_request`, GitHub runs the workflow and `pr_guard.py` from the merge ref, so a PR that edits them runs its own edited copy. Any session can also add the label. | Check `.github/workflows/*` and `scripts/ci/*` for `claude/*` PRs in `lead-merge.ps1`, which runs from the lead's checkout. Correct the sentence. |
| N4 | `pr_guard.py:144,191`; `docs/CI-GATE-RUNNER.md:69` | Medium | `workflow_errors` misses `*.yaml` files and a label-only `runs-on: glancelot-gate`; both returned 0 errors in my test. It also runs alongside the workflow it inspects, so it cannot stop that run. Not a new route (see "Checked and sound"). | Scan both extensions. Fail any `runs-on` label outside a hosted allowlist when a PR-type trigger is present, including `pull_request_target` and `workflow_run`. Say in the runbook that the barrier is the approval policy and the lint is a backstop. |
| N5 | `pr_guard.py:33`; `lead-merge.ps1:16-17`; `AGENTS.md:75` | Medium | Three "docs" paths are build inputs. `Dockerfile:32` and `scripts/build.ps1:44` copy `README.md` and `THIRD_PARTY_NOTICES.md`, so deleting either breaks Docker and packaging with no build run. `.gitattributes` holds `*.sh text eol=lf` and is both docs-only and bookkeeping, though AGENTS.md:75 does not list it. I found no build or test that reads `docs/`. | Remove `.gitattributes` from both classifiers; the new `integrate-*` line makes routine edits unnecessary. Treat root `README.md`, `THIRD_PARTY_NOTICES.md` as code when deleted or renamed, or always. |
| N6 | `pr_guard.py:150,189` | Low | Non-UTF-8 bytes under `tests/` crash the guard with a traceback and no override (reproduced; no such file exists today). A PR that deletes the queue also tracebacks. Both fail closed. | Pass `errors="replace"`. Catch the `git show` failure and emit `::error::`. |
| N7 | `lead-merge.ps1:29` | Low | A close and reopen, the N1 workaround, can leave two `ci-ok` entries (expected from gh's own de-duplication; not reproduced here). `Select-Object -First 1` may read the old one and refuse a good PR. Branch protection still guards the other direction. | Sort by `completedAt` descending first. |
| N8 | `pr_guard.py:27-28` | Low | Lint misses `QTest::qSleep`, `qWaitFor(..., 500)`, `QSignalSpy::wait(100)`, bare `msleep`, multi-line QTRY calls and plain `QTRY_*` with its 5 s default (the first four and the multi-line case returned 0 errors). | Extend the patterns. Treat as best effort; the review rule still applies. |
| N9 | `build.yml:27-29,39-41`; `AGENTS.md:12` | Low | A lint or title failure skips `core` and `viewer`, so a cloud implementer gets no Windows compile feedback from a draft PR until the lint passes. | `if: ${{ !cancelled() && needs.guard.outputs.code == 'true' }}`. `ci-ok` still fails on the guard. |
| N10 | `lead-merge.ps1:32,46` | Low | A fork PR with a `lead/` branch name counts as bookkeeping, and the delete targets a same-named branch in this repository. A failed delete still prints "deleted". | Add `isCrossRepository` to the view and refuse those. Check `$LASTEXITCODE` after the delete. |
| N11 | Repository settings | Setup | Read-only checks show: `main` is not protected yet ("Branch not protected"), though `AGENTS.md:58` and `docs/CI-GATE-RUNNER.md:63` say it is; `allow_auto_merge` is false, so `-Auto` will fail; labels `ci-known-red` and `lead-grant` do not exist, so adding them fails. | The lead or owner applies all three right after this merges, and confirms the first with `gh api`. I changed nothing. |
| N12 | `AGENTS.md:8` against `:11,:83` | Low | "Nobody pushes to another session's branch: message that session" conflicts with the fresh cloud fix session on the same branch, with cloud sessions that cannot be messaged, and with the lead merging `main` in for the landing lane. | Add the carve-outs: the lead's merge of `main`, and a fix session replacing a finished one. |
| N13 | `AGENTS.md:16` | Low | It still calls `build.yml` an early warning triaged against a full gate, but `ci-ok` now makes it required. History is not alarming: 89 of 98 finished runs passed. | State that a `build.yml` failure blocks unless handled through `ci-known-red`. |
| N14 | `AGENTS.md:74`; `windows-gate.yml` | Low | Only "the tester" posts `local-gate`. When `lead-windows-gate` runs the gate, nothing posts it. | Say that the lead posts it after checking the run, naming the run URL. |
| N15 | `LEAD-PLAN-2026-10-22.md:51,263`; `WORK-QUEUE.yaml:6`; `pr_guard.py:9` | Low | The current plan still describes `integration_lock`, `gate/*` and `--no-ff` integration. The `integration_lock` key has no rule left. The docstring says `wsl -e python3` where AGENTS.md says `python`. | Add a pointer in the plan to the new section, remove the key, and align the docstring. |

**Not a defect, but the lead should decide:** `guard` fails `claude/stack-confirm-fix` (26 errors) and `claude/phase1-followups` (6 errors) on added `qWait` and sub-10 s deadlines, as the rule intends. `lead/integrate-cycle1-h` contains `phase1-followups` and passes, because integration branches skip the lint. `claude/stack-confirm-fix` is in neither `main` nor `lead/integrate-cycle1-h`, so if it gets its own PR it needs `// sleep-ok:` markers or rewritten waits.

## Checked and sound

- **`ci-ok` reporting.** No `paths` filter; `if: always()` runs it after a failed, skipped or cancelled dependency.
- **Wrong passes.** A skipped or cancelled `guard` exits 1. The docs-only exit needs guard success, `code` not true and both builds skipped. A cancelled build without the label exits 1.
- **Stuck PRs.** Cancel-in-progress affects only superseded heads, and the newest head gets its own run. A conflicted PR gets no run and stays blocked until resolved, which is safe.
- **Rerun.** `gh run rerun --failed` reruns the failed jobs and their dependents. `ci-ok` reads labels live, and a failed `gh` call falls through to `exit 1`.
- **Merge ref.** With `fetch-depth: 0` the head is a parent of the merge commit and the base is on a fetched branch, and the three-dot range is right. The `guard` job on run 41933295517 concluded `success` on the merge ref.
- **No route to the runner.** `windows-gate.yml` triggers only on `push: gate/**` and `workflow_dispatch`, and only its line 78 uses the runner. `build.yml` is hosted only. The fork approval policy reads `all_external_contributors`. Only someone with write access can push `gate/**` here.
- **Real queue and history.** Queue at head parses with 85 tasks, all IDs fit the title rule, no ID is a prefix of another. I replayed the guard over the last 60 merges on `main` and found no false failure on the current queue format. Failures were the intended lint and size rules, old queue revisions that really do not parse or predate the queue, and scope or lead-owned hits caused by the `claude/` names I forced on old `agent/` and `codex/` branches.
- **Status lookups.** Combined status holds `fresh-review` and `local-gate`, latest per context. `statusCheckRollup` exposes `name` and `conclusion` (`guard`, `SUCCESS`). `--merge --match-head-commit --auto` are all valid in gh 2.103.3.
- **Branch delete.** Safe for same-repository PRs, including stacked children, which GitHub retargets. Dry run of the script's gate logic on PR 1 (merge lines removed) reported `bookkeeping=False docsOnly=False` and the three expected problems.

## Not verified

- **`ci-ok` on a real run.** The `viewer` job of run 41933295517 was still running when this was written (it takes about 38 minutes), so `ci-ok` had not reported and the lookup returned nothing yet. The next reviewer should confirm `ciOk=ci-ok/SUCCESS` on the fixed head.
- **Docs-only path on a real PR.** Read from the logic only.
- **`gh pr view --json labels` with this job token.** The path has not run. It fails closed, so the worst case is that the exemption does not work.
- **Duplicate rollup entries (N7).** Not reproduced.
- **The full gate.** I did not build. All guard runs used Windows Python 3.15.2 with PyYAML 6.0.2 on local throwaway commits, now discarded. `scripts/ci/test_pr_guard.py` passed (29 tests).
