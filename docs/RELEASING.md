# Commits, versions and releases

Written for: every session that commits to Glancelot, and the lead when it prepares a release. The rule took effect on November 6, 2026 (decision: `docs/decisions/2026-11-05-conventional-commits-semver.md`).

The chain this keeps verifiable:

```text
Conventional Commit -> semantic version -> immutable Git tag -> build with provenance -> About / --version
```

## Commits

Every commit subject follows [Conventional Commits](https://www.conventionalcommits.org/):

```text
type(scope): imperative description
```

- **Subject:** at most 100 characters, imperative, lowercase type and scope, and no full stop at the end. The scope is optional.
- **Body:** after a blank line, say why, plus anything a reviewer needs.
- **Trailers:** packet work adds `Task: <TASK-ID>`, for example `Task: A1`, so release notes and reviews can trace a change to its queue row. Keep the `Co-Authored-By` attribution line where it applies.

| Type | Use | Version effect |
| --- | --- | --- |
| `feat` | A capability a user can see or use | minor |
| `fix` | A defect a user or operator can notice | patch |
| `perf` | A measured performance improvement; put the numbers in the body | patch |
| `refactor` | Restructuring with no behaviour change, such as EN-07's command move | none |
| `test` | Tests only | none |
| `docs` | Documentation, handoffs, reviews, evidence, the queue | none |
| `build` | CMake, dependencies, Qt, Docker image, packaging | none |
| `ci` | `.github/workflows`, `scripts/ci`, gate and merge tooling | none |
| `chore` | Maintenance that fits no type above | none |
| `revert` | Reverting an earlier commit; name its SHA in the body | as reverted |

**Breaking changes** add `!` after the scope (`feat(settings)!: ...`) or a `BREAKING CHANGE:` footer describing what breaks and how users recover. For a desktop viewer, breaking means:
- a settings, workspace, recovery or journal file an earlier version can no longer read, or that this version can't read from an earlier one;
- a removed or renamed command-line option;
- a removed feature;
- a shortcut whose meaning changes.

**Scopes** name durable subsystems, not directories or tasks. Prefer these:

| Scope | Area |
| --- | --- |
| `grid`, `viewer`, `canvas`, `tree`, `slideshow`, `compare` | Browsing and viewing surfaces |
| `ui`, `theme`, `a11y`, `i18n`, `input` | Shared QML, theme tokens, accessibility, translations, shortcuts and command routing |
| `decode`, `render`, `cache`, `io`, `fs`, `platform`, `monitor`, `recovery` | Core subsystems |
| `ops`, `export`, `metadata`, `assets`, `journal` | File operations, export, metadata and sidecars, asset grouping, the operation journal |
| `contracts` | Frozen headers and `docs/contracts/` |
| `docker`, `deps`, `release` | Container, third-party dependencies, versions and releases |
| `guard`, `gate` | CI checks, and the gate and merge tooling |
| `queue`, `handoff`, `review`, `evidence` | Lead and review bookkeeping |

`guard` checks the scope's form (lowercase words joined by hyphens), not this list. Add a new scope here in the same PR that first uses it.

**Merge commits** are exempt, whatever their subject. That covers GitHub's `Merge pull request #N ...`, git's `Merge remote-tracking branch ...` and the lead's `merge: origin/main into ... (lead, landing lane)`. A non-merge commit whose subject only starts with "Merge" is not exempt. Git's and GitHub's own `Revert "<subject>"` is accepted; name the reverted SHA in the body. `fixup!` and `squash!` commits are fine locally but must be squashed before pushing.

**Pull request titles** follow the same format, because GitHub's merge commit carries the title. A packet PR ends its title with the task, for example `feat(grid): incremental cancellable folder listing (A1)`, and lead bookkeeping uses `docs(queue): ...` or `docs(evidence): ...`. PRs 1 to 18 were opened before the rule. They keep their `<TASK-ID>: <title>` titles, and they may still add commits in that form, because their branches are never reworded. Every other format is still refused there.

**Enforcement:**
- `scripts/ci/pr_guard.py` (the `guard` job, so `ci-ok`) fails a PR whose title, or any non-merge commit committed on or after November 6, 2026, breaks the format. Earlier commits are exempt: shared history is never rewritten to fit this rule (AGENTS.md: no rebasing or force-pushing shared branches).
- The local hook `scripts/hooks/commit-msg` runs the same check as you commit. It is opt-in: enable it once per clone with `git config core.hooksPath scripts/hooks`. It is enabled on the shared clone on the lead's PC only if the user turns it on, because it is persistent configuration. `guard` enforces the rule either way. To check a branch before pushing: `python scripts/ci/commit_lint.py --range origin/main..HEAD`.

Examples:

```text
feat(grid): show more rows in folders over 100,000 entries
fix(grid): keep a menu selection made while a folder loads
perf(fs): list directories on a worker with bounded batches
refactor(ui): move feature commands into their components
test(recovery): wait for the checkpoint instead of sleeping
docs(evidence): record the PR 9 gate
ci(guard): check commit subjects and PR titles
build(docker): pin the Qt archive checksum
```

## Versions

- **One source.** The version is `project(Glancelot VERSION X.Y.Z)` in the root `CMakeLists.txt`. Nothing else holds a hand-written version. Every other copy (About, `--version`, the Windows file version, Docker labels) is derived from it at build time. Never write a Git SHA into a source file.
- **[Semantic Versioning](https://semver.org/) 2.0.0,** `MAJOR.MINOR.PATCH`.
- **While the version is 0.y.z** (it is 0.1.0 now): a breaking change or a `feat` raises MINOR, and `fix` or `perf` raises PATCH. 1.0.0 is a product decision for the user, and after it a breaking change raises MAJOR.
- **Pre-releases.** Use `X.Y.Z-rc.N` for release candidates and `X.Y.Z-dev.N` for development builds that are handed out. A pre-release suffix is passed to the build as the CMake cache variable `GLANCELOT_VERSION_SUFFIX` (REL-01), never edited into `project()`. Promote only a commit that has passed the gate.

## Releases

Publishing is outward, so only the lead prepares a release, and only after the user approves that release. Checklist:

1. Start from `main` on a commit that passed the full Windows and Docker gate, with a clean tree, `git fetch --tags`, and the current version read from `CMakeLists.txt`.
2. List commits since the previous tag (`git log --no-merges vPREV..HEAD --format=%s`), check each one's type against its diff, and choose the version from the table above.
3. Raise the version in `CMakeLists.txt` in a `chore(release): X.Y.Z` PR through the normal merge gate, with the release notes, grouped by `feat`, `fix` and `perf` and naming every breaking change, in `docs/releases/X.Y.Z.md`.
4. Tag the merged commit `vX.Y.Z` (annotated). Tags are immutable: never move or delete a published tag, and fix a bad release with a new patch.
5. Build once from that tag with provenance injected, and ship the same artifacts under every name. Docker images get both `X.Y.Z` and `sha-<full SHA>` tags, plus the OCI `version`, `revision` and `created` labels. `latest`, if used at all, is only a convenience alias.
6. Check the built app's About and `--version` show the version and short SHA, and record the artifacts' digests as the rollback target.

## Build provenance

Every distributable build carries the semantic version, the full Git SHA (with the short SHA for display), a UTC ISO 8601 build time taken once per build, and the build kind (`release`, `rc`, `dev` or `local`). The app shows them in About and prints them for `--version`. No network is involved: the viewer sends nothing anywhere (AGENTS.md). A release build fails if it can't determine the SHA.

Queue row REL-01 implements this. Until it lands, builds carry only the CMake version, and no release should be made.
