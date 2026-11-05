# Conventional Commits going forward; history left as is (2026-11-05)

## Decision

- Every new commit subject follows Conventional Commits 1.0:
  `type(scope): imperative description`, `!` or a `BREAKING CHANGE:` footer for
  breaking changes. Types: feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert.
- Version impact (SemVer): `feat` = MINOR, `fix`/`perf` = PATCH, breaking = MAJOR; the rest do not release.
- Task and bookkeeping prefixes move into the scope: `fix(b3-export): ...`,
  `docs(lead): PR 9 evidence`, instead of `B3: ...` or `LEAD: ...`.
- Git/GitHub generated subjects (`Merge pull request ...`, `Merge branch ...`, `Revert "..."`) are exempt.
- Enforcement: `.githooks/commit-msg` (enable with `git config core.hooksPath .githooks`)
  runs `scripts/check_commit_msg.py`. Audit a range with
  `python3 scripts/check_commit_msg.py --range origin/main..HEAD`.

## Past commits are not rewritten

At the time of writing, 155 of 369 non-merge commits do not conform (mostly `LEAD:`,
`B3:` and `STACK-CONFIRM-FIX:` prefixes). Rewriting them would change every SHA
after the first rewritten commit. AGENTS.md forbids rebasing or force-pushing shared
branches and protects `main`; merged PRs, handoffs, reviews, `fresh-review`/`local-gate`
statuses and evidence all cite existing SHAs. The semantic-versioning policy also advises
against cosmetic rewrites of shared history. The convention is enforced from here on.

## Follow-ups for the lead (lead-owned files)

- PR title rule `<TASK-ID>: <title>` should become `type(<task-id>): <title>` so merge/squash
  subjects conform; add the check to `scripts/ci/pr_guard.py` / `build.yml`.
- Choose a release tool (e.g. release-plz/semantic-release) and a single version source before tagging.
