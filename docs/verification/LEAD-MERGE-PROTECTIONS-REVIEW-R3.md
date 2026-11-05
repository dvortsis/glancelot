# LEAD-MERGE-PROTECTIONS review, round 3

**Verdict: APPROVED**

Written for: the lead, who merges PR 1 once `ci-ok` is green on this head.

- Pull request: https://github.com/dvortsis/glancelot/pull/1
- Reviewed: `git diff aac951354e87cc94c00cd0194f2105f644673ba9 3b7283ec429aab525da10e80495290f781b784e4`
- Earlier review: `docs/verification/LEAD-MERGE-PROTECTIONS-REVIEW-R2.md` on `origin/claude/review-merge-protections-r2`
- Reviewer: fresh-context Opus session that wrote neither the PR nor an earlier review.

B3 is fixed: the drift check reported every case of a head missing code on `main`, and it refused on a bad SHA, a failed fetch and a failed comparison. The one gap I found needs an empty head SHA, which the GitHub API does not return (N25). No merges are blocked for all PRs, no bad merge gets through, and the self-hosted runner is not reachable from a PR. The new findings are three low ones, and none needs to hold this merge.

## B3: the drift check

I tested a copy of `scripts/lead-merge.ps1` with the merge and delete calls removed. `gh` was stubbed, so the PR looked ready and only the drift block decided. Every case ran under PowerShell 7.6.9 unless marked otherwise. Windows PowerShell 5.1 ran the real-origin, merge-base, rename and non-ASCII cases, with the same results.

**Real origin, Glancelot checkout**

| Case | Result |
| --- | --- |
| Head is `origin/main` | `WOULD-MERGE` |
| Head `840fa38f` (`008d026951^1`), a range of 1,275 files | Drift, 105 files, the same count as a separate `git diff` |
| Head is PR 1 | Drift, 9 files |
| Bad SHA `deadbeef...` | Throws `Could not fetch main and the head.`, exit 1 |
| Unreachable proxy | Git reports the failed connection, then the same throw, exit 1 |
| Not a git checkout | Same throw, exit 1 |
| Empty SHA (stubbed; the API does not return this) | No throw. Git reads `...origin/main` as `HEAD...origin/main`, so it compared this checkout's HEAD and reported 9 files only because HEAD was PR 1's head. See N25 |

**Local origin, one change on `main` at a time**

| Change on `main` | Result |
| --- | --- |
| Docs added, modified or deleted; root `README.md` modified | `WOULD-MERGE` |
| `src/` file added, modified or deleted | Drift, 1 file |
| `README.md` or `THIRD_PARTY_NOTICES.md` deleted | Drift, 1 file |
| Rename `README.md` to `docs/`, or `src/a.cpp` to `docs/` | Drift, 1 file (the delete half) |
| Rename `docs/a.md` to `src/` | Drift, 1 file (the add half) |
| Type change to a symlink: `src/` | Drift, 1 file |
| Type change to a symlink: `docs/` | `WOULD-MERGE` |
| 400 files under `docs/` | `WOULD-MERGE` |
| 400 files under `docs/` plus `ui/z.qml` | Drift, 1 file, so the file past the old 300 limit is counted |
| Non-ASCII file in `docs/`: `résumé 日本.png` | `WOULD-MERGE` |
| Non-ASCII file in `src/`: `é.cpp` | Drift, 1 file |
| `scripts/ci/t.py` | Drift, 1 file, see the note below |

**Merge bases**

| Case | Result |
| --- | --- |
| Unrelated history: fetch works, `git diff` exits 128 | Throws `Could not compare the head with main.`, exit 1 |
| Criss-cross merge, two merge bases, same tree | Drift, 1 file. A false alarm, in the safe direction |
| Criss-cross merge, `main` then adds code | Drift, 2 files |
| Head has merged current `main` | `WOULD-MERGE` |
| `main` deletes code after that merge | Drift, 1 file |

**Mechanics confirmed**

- **Native exit codes.** `$ErrorActionPreference = 'Stop'` does not act on them, so the explicit `$LASTEXITCODE` checks at `scripts/lead-merge.ps1:61,63` are what refuse. Both throw paths fired on both PowerShell versions.
- **Stale `origin/main`.** The clone in the local cases fetched nothing except through the script, and it still saw each new `main`. The fetch updates `origin/main` under the normal `+refs/heads/*` refspec, which this repository uses.
- **Status letters.** Git emitted `A`, `M`, `D` and `T`, and the `$status, $path = $_ -split` assignment read them correctly inside `Where-Object`. Only `D` passes `removed`. A deleted docs file passes, and a deleted root `README.md` counts as code.
- **Paths git still quotes.** A docs path holding a tab or a double quote comes back quoted even with `core.quotepath=off`, so it fails the `docs/*` test and counts as drift. That is the safe direction, and no such path exists here. Tabs stay escaped, so the split cannot break.

**Real PR 1, real `gh`, no-merge copy.** It refuses with `ci-ok` not green, no `fresh-review`, no `local-gate` and 9 files of drift. With `-Auto` it also reports that `-Auto` is refused for code. That is expected for this bootstrap.

**Note: CI tooling on `main` counts as drift.** A change to `.github/*` or `scripts/ci/*` on `main` makes every waiting code PR merge `main` in and gate again, even though such a PR itself needs no `local-gate`. It costs a gate run and cannot let a bad merge through. I would leave it as it is, because those files can change what the gate does.

**Note: the check is not atomic with the merge.** `main` can move between the fetch and `gh pr merge`, and `--match-head-commit` pins only the head. Only the lead merges and integration is serial, so this needs the lead to race itself.

## N16 to N22

| # | Status | Evidence |
| --- | --- | --- |
| N16 | Fixed | `scripts/lead-` is in `LEAD_ONLY`, `Test-LeadOnly` and both `AGENTS.md` lists. In a temporary repository, `scripts/lead-merge.ps1` from `claude/x` gives one error. Only `lead-inbox.ps1` and `lead-merge.ps1` match, so nothing else is caught. |
| N17 | Fixed | See the table below. |
| N18 | Fixed | The `// sleep-ok:` sentence in `AGENTS.md` now names backoff in a fake and a hang loop in a child process. It was not on the check list, but it is in the diff. |
| N19 | Fixed | In a temporary repository, `scripts/ci/café.py` gives one error with the flag and none without it, because git prints `"scripts/ci/caf\303\251.py"`. A non-ASCII file in `docs/` now reads `code=False`. See N24. |
| N20 | Fixed | A matrix entry `glancelot-gate` on a `pull_request` workflow is now refused, a hosted matrix passes, and the real `build.yml` and `windows-gate.yml` give no errors. A comment naming the label also refuses, which is fine for a backstop. See N24. |
| N21 | Fixed | The merge gate in `AGENTS.md` now allows the lead to post `fresh-review` on a head that only merges reviewed work and `main` without conflicts. A conflict resolution in code needs a reviewer. This agrees with the integration rule. |
| N22 | Fixed | Review documents go in a `lead/` bookkeeping PR, and review branches are never merged. `Test-Bookkeeping` already accepts `docs/verification/*`. |

**N17 on real lines.** I compared `wait_errors` with a small balanced-parenthesis parser on every wait in `tests/`: 486 candidate lines.

| Line | Before | Now | Correct |
| --- | --- | --- | --- |
| `tests/fs_watch_tests.cpp:772` | Failed, read as 0 | Clean | Clean, 10000 |
| `tests/fs_watch_tests.cpp:1029` | Failed | Clean | Clean |
| `tests/grid_ui_tests.cpp:639` | Failed | Clean | Clean, 15000 |
| `tests/monitor_customization_ui_tests.cpp:125` | Failed | Clean | Clean, 15000 |
| `tests/fs_watch_tests.cpp:1133` | Missed | Flagged | Short, 5000 |
| `tests/reference_pin_tests.cpp:65` | Flagged | Flagged | Short, 5000, no trailing `;` |
| `tests/recovery_tests.cpp:1105` | Flagged | Flagged | Short, 3000, a lambda with `;` inside |
| `tests/folder_tree_tests.cpp:533` | Flagged | Flagged by `SLEEP` | A real `msleep` |

Across all 486 lines the head pattern has 0 false failures and 0 misses against that parser, where the old one had 4 and 1.

**Fail-before.** With the N17 change reverted, `test_each_wait_on_a_line_is_read` fails. With `scripts/lead-` removed, `test_packet_cannot_change_the_gate` fails.

**`python scripts/ci/test_pr_guard.py`.** 36 tests pass on Windows. The hosted `guard` job also passed on this head.

## New non-blocking findings

| # | Where | Severity | Finding | Fix |
| --- | --- | --- | --- | --- |
| N23 | `scripts/ci/pr_guard.py:31` | Low | The lazy `qWaitFor` form has no end anchor, so it can stop inside the lambda. `QVERIFY(QTest::qWaitFor([&] { return m.index(0, 0).isValid(); }, 10000));` reads as a deadline of 0 and fails. The `QTRY` form has a smaller gap: `QTRY_VERIFY_WITH_TIMEOUT(a(), kDeadline);QCOMPARE(b(), 3);` reads as 3. No line in `tests/` has either shape today. The cost is a false failure on one PR, and the author can split the line. | For `qWaitFor`, use `qWaitFor\s*\(.*?,\s*(\d+)\s*\)\s*(?:\)\|;\|$)` (tested on these cases). Do not change the `QTRY` form to `[^;]*?`: that misses `tests/recovery_tests.cpp:1105`. |
| N24 | `scripts/ci/test_pr_guard.py` | Low | The N19 and N20 fixes have no test. Reverting either leaves all 36 tests passing, so a later edit could undo them unnoticed. I confirmed both by hand. | Add a non-ASCII `scripts/ci/` path to the temporary-repository tests, and a matrix `glancelot-gate` case to the workflow tests. |
| N25 | `scripts/lead-merge.ps1:32,62` | Low | An empty `$sha` makes the drift check compare the local HEAD, so from a checkout on `origin/main` it would pass. It cannot be reached in practice: `$pull.head.sha` comes from the API, and with an empty SHA the `commits//status` lookup should fail or find no statuses, so the script stops or reports `fresh-review` and `local-gate` as missing (reasoned, because that call was stubbed). | Optional, after line 32: `if ($sha -notmatch '^[0-9a-f]{40}$') { throw "PR $Pr has no usable head SHA." }` (not run). |

None of these needs to hold PR 1.

## No route to the runner

The diff changes no workflow.

- `windows-gate.yml` has only `push: gate/**` and `workflow_dispatch`, and no `workflow_call`, so a reusable-workflow `uses:` cannot start it.
- Its job is the only one on `[self-hosted, windows, glancelot-gate]`.
- The repository's fork approval policy reads `all_external_contributors`. That is the real barrier, as `pr_guard.py` says.

## PR 1 merged directly

No objection to `gh pr merge --merge --match-head-commit 3b7283ec429aab525da10e80495290f781b784e4`, with these conditions:

- `ci-ok` is green on this head. When I last looked, `guard` and both `core` legs had passed and `viewer` was still running.
- A PR comment records that this is a one-time bootstrap.
- A new push needs a new `fresh-review`.

The script's own rule would refuse PR 1, as round 2 explained, and the exception ends once the script is on `main`.

## Not verified

- **`ci-ok` on this head.** As briefed, I did not wait for `viewer`.
- **N11.** Protecting `main` and creating the `ci-known-red` and `lead-grant` labels are still to do, right after the merge.
- **The live merge path.** The `gh pr merge` calls were removed from every copy I ran. The drift block used a stubbed `gh`, except for the dry run on PR 1.
- **Consoles with a non-UTF-8 code page.** The runs reported UTF-8. A different ASCII-compatible code page could garble the non-ASCII part of a path, but `docs/` and `.md` are ASCII and would still match. This is reasoned, not run.
- **The full gate.** I did not build. Nothing merged, and I changed no repository settings.
