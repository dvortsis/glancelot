# LEAD-MERGE-PROTECTIONS review, round 2

**Verdict: CHANGES REQUIRED**

Written for: the lead, who applies the one blocking fix before PR 1 merges.

- Pull request: https://github.com/dvortsis/glancelot/pull/1
- Reviewed head: `aac951354e87cc94c00cd0194f2105f644673ba9`, merge ref `2a7c7d7` (parents `b1d57cd`, `aac9513`)
- Earlier review: `docs/verification/LEAD-MERGE-PROTECTIONS-REVIEW.md` on `origin/claude/review-merge-protections` (`1d16642`)
- Reviewer: fresh-context Opus session that wrote neither the PR nor the earlier review.

The B1 check exists and looks the right way, but it fails open in three ways, so an ungated tree can still merge. Everything else held up. No PR can reach the self-hosted runner, and ordinary PRs are not blocked.

## Blocking

### B3. The check that `main` holds no code the head lacks can pass when `main` does hold code the head lacks

`scripts/lead-merge.ps1:59-60`

The direction is right. With real SHAs, `compare/aac9513...main` lists the 24 files of PR 2, and the reverse lists PR 1's own 12 files. The dry run on PR 1 reports "main has code this head lacks (9 files)". The problem is how the call can come back incomplete.

| Way through | Evidence | Result |
| --- | --- | --- |
| **The compare API lists at most 300 files.** `--paginate` pages commits, not files, and files come back in path order, so `docs/` fills the list ahead of `include/`, `src/`, `tests/`, `tools/`, `translations/` and `ui/`. | Real range `008d026951^1...008d026951`: git shows 418 files, 30 of them code. The script's exact call lists 300 files, 2 of them code. The 1,854-file range `e39eee20f7` also lists 300. Stub run with 300 docs entries: `WOULD-MERGE`. | Hidden code is not counted. |
| **A failed call is not checked.** `$ErrorActionPreference = 'Stop'` does not catch a native exit code. | With an unreachable proxy, the call returned `count=0 exit=1`. Stub run with that failure: `WOULD-MERGE`. An HTTP 404 refuses only by accident, because its JSON body on stdout fails the docs test. | A network failure reads as no drift. |
| **Renames on `main`.** The call reads `.filename` but not `previous_filename`, which is the gap B2 fixed for the PR's own file list. | Stub run with `renamed` to `docs/b.md`: `WOULD-MERGE`. | A move out of `src/` reads as docs. |

Failure: packets A and B are gated on the same `main`. A lands with code only under `src/`, `tests/` and `ui/`. The evidence for A lands in a bookkeeping PR with 300 or more files. B then passes, and the A+B tree has never been gated. Evidence this size is real: `safe-mode-01` holds 2,409 files, `startup-window-01` 725 and `monitor-custom-01` 446, and two of the last 60 merges on `main` changed more than 300 files.

The earlier review proposed this call, so the gap came from that suggestion, not from how the lead applied it.

Fix, replacing lines 59-60 (tested on this head: 9 code files on PR 1, 30 of 30 on the `008d026951` range, exit 128 on a bad SHA):

```powershell
git fetch --quiet origin main $sha
if ($LASTEXITCODE) { throw 'Could not fetch main and the head.' }
$diff = @(git -c core.quotepath=off diff --name-status --no-renames "$sha...origin/main")
if ($LASTEXITCODE) { throw 'Could not compare the head with main.' }
$drift = @($diff | Where-Object {
    $status, $path = $_ -split "`t"
    -not (Test-DocsOnly $path $(if ($status -eq 'D') { 'removed' } else { 'modified' })) })
```

`core.quotepath=off` matters. Without it, git quotes 196 non-ASCII evidence paths in the `e39eee20f7` range, they fail the `docs/*` test, and the code count reads 236 instead of 40. The script then has to run from a Glancelot checkout, as it does in practice. Say so in its header, because `-Repo` no longer covers this check.

A round 3 review can be limited to this block and anything else changed after `aac9513`.

## Status of the earlier findings

| # | Status | Evidence |
| --- | --- | --- |
| B1 | **Reopened as B3** | Check added and direction correct. It fails open as above. `-Auto` is correctly refused for code PRs. |
| B2 | Fixed | Stub runs: a move to `docs/handoff/` needs review and gate, and a move out of `scripts/ci/` from `claude/` is refused. A short list throws ("lists 1 of 3 files"). PR 2's list reads 24 of 24 at `per_page=30` and at `per_page=5` (5 pages). `pr_guard.py:177` uses `--no-renames`; two rename tests run in a temporary repository. |
| N1 | Fixed | The guard step reads labels, title and body live. Stub harness: empty body, quotes, `$(...)`, backticks and newlines are all safe, with no injection. `Large PR:` on a later line is honoured. The event's stale title is ignored. |
| N2 | Fixed | Matrix below. |
| N3 | Fixed | Decision document corrected. The merge script checks `lead-grant`. |
| N4 | Fixed | `*.yaml` is scanned, labels are checked, and the runbook calls the lint a backstop. See N20 for what remains. |
| N5 | Fixed | `.gitattributes` is code. Deleting or moving root `README.md` or `THIRD_PARTY_NOTICES.md` needs the gate (stub runs, and `test_deleting_a_shipped_doc_builds`). No other `.md` outside `docs/` is a build input (`git grep` of CMake, Dockerfile and `build.ps1`). |
| N6 | Fixed | `errors="replace"`, and `show_queue` catches the failure. |
| N7 | Fixed | Sorted by `completedAt`. A real duplicate is still not reproduced. |
| N8 | Partly fixed | `qSleep`, `qWaitFor`, `.wait(n)` and bare `msleep` are caught. Multi-line and plain `QTRY_*` are stated as best effort. See N17. |
| N9 | Fixed | `!cancelled()` on `core` and `viewer`. |
| N10 | Fixed | Forks are refused (stub run), and the delete checks its exit code. |
| N11 | Open, after merge | Read-only today: `main` is not protected, and labels `ci-known-red` and `lead-grant` do not exist. The lead does this right after the merge. |
| N12 | Fixed | Both exceptions are in `AGENTS.md:8`. |
| N13 | Fixed | `AGENTS.md:16`. |
| N14 | Fixed | `AGENTS.md:74` says the lead posts `local-gate` after checking the run. |
| N15 | Fixed | Pointer added to the plan, `integration_lock` removed from the queue, and nothing in `scripts/` or `.github/` reads it (`git grep`; other hits are history documents and test fixtures). |

## New non-blocking findings

| # | Where | Severity | Finding | Fix |
| --- | --- | --- | --- | --- |
| N16 | `scripts/lead-merge.ps1:25`; `scripts/ci/pr_guard.py:23`; `AGENTS.md:7,93` | Medium | `scripts/lead-*` is CI tooling, so it needs no gate, but it is not lead-owned. A `claude/` PR can change `lead-merge.ps1`, which `AGENTS.md:93` calls the real check, with no `lead-grant` (stub run: `WOULD-MERGE`). A reviewer still has to pass it. | Add `scripts/lead-` to `LEAD_ONLY`, `Test-LeadOnly` and both AGENTS.md lists. |
| N17 | `scripts/ci/pr_guard.py:31,117` | Low | The greedy `.*` picks the last `, N)` on a line, and only the first pattern match is read. This repository writes several statements per line. Over 1,489 candidate lines in `tests/`, 4 correct lines would fail, such as `QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::red));` at `tests/fs_watch_tests.cpp:772`, which reads as a deadline of 0. One short deadline is missed at `tests/fs_watch_tests.cpp:1133`. `// sleep-ok:` is an escape, but the reason would be untrue. | Use `QTRY_\w+_WITH_TIMEOUT\s*\(.*?,\s*(\d+)\s*\)\s*;` and read every `finditer` match. Keep the `qWaitFor` form able to match without a trailing `;`, since `tests/reference_pin_tests.cpp:65` is a real short deadline with none. |
| N18 | `AGENTS.md:100` | Low | The `sleep` and `.wait(n)` patterns are accurate: of the 40 hits I sampled, each sleep was a real sleep and each `.wait(n)` a real timed wait. Some are not test waits at all: backoff in fakes and hang loops in child processes, such as `while(!release) QThread::msleep(5);` at `tests/folder_tree_tests.cpp:533`. AGENTS.md names only "asserting that nothing fires" as the exception, so a session may rewrite a fake needlessly. | Add those two cases to the `// sleep-ok:` sentence. |
| N19 | `scripts/ci/pr_guard.py:177,195` | Low | Git quotes non-ASCII paths, so `"docs/...\351..."` reads as code and evidence-only PRs run full builds (196 such paths in `e39eee20f7`). It also counts toward size. The same quoting would let a non-ASCII file under `scripts/ci/` pass `lead_only_errors`. Mostly the safe direction. | Call `git -c core.quotepath=off` in both. |
| N20 | `scripts/ci/pr_guard.py:164` | Low | Labels containing `${{` are skipped, and a job with `uses:` has no labels, so a matrix entry `glancelot-gate` or a reusable workflow gets past the lint. It is a backstop only. The real barrier holds. | Optional: read matrix values, or fail on the runner's distinctive label anywhere in the text, as is done for `self-hosted`. |
| N21 | `AGENTS.md:73` against `:83` | Low | The merge gate says a reviewer posted `fresh-review`, but the integration rule has the lead post it, naming the packet reviews. The landing lane has the same gap, because merging `main` in clears both statuses. | In line 73, add "or the lead, for a head that only merges reviewed work and `main` without conflicts; a conflict resolution needs a reviewer". |
| N22 | `AGENTS.md:25,83` | Low | Review documents go on `claude/review-*`, and nothing says how they reach `main`. A PR from that branch is not bookkeeping, so it would need its own `fresh-review`. | Say the lead carries review documents in the `lead/` bookkeeping PR. |

## PR 1 merged directly

No objection to `gh pr merge --merge --match-head-commit` once B3 is fixed, since the script is not on `main` yet. Conditions:

- `ci-ok` is green on the approved head, which is the new head after the B3 fix.
- A PR comment records that this is a one-time bootstrap.

Reasons:

- The `.gitattributes` line affects only `docs/verification/integrate-*`, which nothing builds, and the `*.sh` rule is unchanged.
- `main`'s code was gated at `e80ca99` (PR 2 body), and PR 1 changes no product code.
- Hosted CI builds the merge ref, so `core` and `viewer` cover PR 1 on top of current `main`.

The script's own rule would refuse PR 1 (no `local-gate`, and 9 code files on `main`). That is expected, and the exception ends once the script is on `main`.

## Checked and sound

**`ci-ok`.** I extracted the step from the workflow and ran it under `bash -e` with a stub `gh`, 24 cases:

| Case | Exit |
| --- | --- |
| guard success, core and viewer success | 0 |
| guard success, both skipped, `code=false` | 0 |
| guard success, both skipped, `code=true` | 1 |
| guard failure, cancelled or skipped, with any build result or label | 1 |
| build failure, no label, attempt 1 or 2 | 1 |
| build failure, `ci-known-red`, attempt 1 | 1 |
| build failure, `ci-known-red`, attempt 2 or 3 | 0 |
| build cancelled or skipped, with label, on a rerun | 1 |
| `gh` fails, or no PR number, or label `ci-known-red-old` | 1 |

**Live runs.**

- Run 41933295517 (`b216dc0`) was superseded and cancelled. Its `ci-ok` logged `guard=success core=success viewer=cancelled code=true` and failed, as intended.
- Run 41936482762 (`aac9513`): `guard` passed on `2a7c7d7`, with `Ran 35 tests ... OK` and `pr_guard: code=True, 0 error(s)` on `ubuntu-24.04`. `core` passed on Windows and Linux. The live `gh pr view` calls worked with this token, including with no labels.

**`test_pr_guard.py`.** 35 tests pass on Windows, on the live Linux job, and in WSL Ubuntu with an empty `HOME` and no system config, so nothing depends on a global identity or default branch.

**`pr_guard.py` on real branches.** With pre-PR 2 `main` as base, `claude/phase1-followups` gives 6 errors and `claude/stack-confirm-fix` gives 26, all true by the rule. Against current `main`, `phase1-followups` gives 0, because it has landed.

**`lead-merge.ps1`.** Scratch copy with the merge and delete lines removed, on real PR 1 and PR 2 and on a stub:

- PR 1 refuses with the four expected problems, and with `-Auto` too. PR 2 refuses as closed.
- Bookkeeping passes on `ci-ok` alone, and fails with `ci-ok` red.
- A docs PR needs `fresh-review`.
- Docs drift passes, and code drift or a removed README on `main` refuses.
- Lead-owned files from `claude/` are refused without the label and pass with it.

**No route to the runner.** `windows-gate.yml` has only `push: gate/**` and `workflow_dispatch`; only `lead-windows-gate` uses `[self-hosted, windows, glancelot-gate]`. `build.yml` is hosted only.

**AGENTS.md.** No contradiction beyond N21 and N22, and no rule that cannot be followed once N11 is done.

## Not verified

- **`ci-ok` on `aac9513`.** `viewer` was still running, so as briefed I did not wait. Confirm it is green before merging.
- **The matrix result under `fail-fast`.** I did not see `needs.core.result` when one leg fails and the other is cancelled. If it reads `cancelled`, the label path refuses until the rerun, which is the safe direction.
- **Docs-only and `ci-known-red` paths on a real PR.** Only the stub harness covered them.
- **Duplicate `ci-ok` rollup entries (N7).** Not reproduced.
- **Whether cloud sessions can open draft PRs.** `AGENTS.md:12` now needs this for Windows compile feedback, since a branch with no PR gets no CI.
- **The full gate.** I did not build. Guard runs used Windows Python and PyYAML on local commits. Nothing merged, and I changed no repository settings.
