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

**Merge commits** are exempt, whatever their subject. That covers GitHub's `Merge pull request #N ...`, git's `Merge remote-tracking branch ...` and the lead's `merge: origin/main into ... (lead, landing lane)`. A non-merge commit whose subject only starts with "Merge" is not exempt. Git's and GitHub's own `Revert "<subject>"` and `Reapply "<subject>"` are accepted, as commits and as PR titles; name the reverted SHA in the body. `fixup!` and `squash!` commits are fine locally but must be squashed before pushing.

**Pull request titles** follow the same format, because GitHub's merge commit carries the title. A packet PR ends its title with the task, for example `feat(grid): incremental cancellable folder listing (A1)`, and lead bookkeeping uses `docs(queue): ...` or `docs(evidence): ...`. PRs 1 to 18 were opened before the rule. They keep their `<TASK-ID>: <title>` titles. Their commits aren't checked, because their branches already mix several older styles and are never reworded, though sessions use the new format there from now on. The lead's `lead/integrate-<n>` batches carry those commits unchanged, so their commits aren't checked either; the lead batches only PRs whose own `guard` passed. A new PR stacked on one of those branches runs that branch's older `guard` until it is retargeted to `main`. Before pushing it, check its own commits with `main`'s copy of the linter: `git show origin/main:scripts/ci/commit_lint.py > <temp file>`, then `python <temp file> --range origin/<parent branch>..HEAD`. To run `pr_guard` locally for an old PR, pass `--pr <number>`.

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

REL-01 implements this in `cmake/Provenance.cmake` and `src/app/build_info.*`.

**CMake cache variables:**

| Variable | Values | Default |
| --- | --- | --- |
| `GLANCELOT_VERSION_SUFFIX` | empty, `-rc.N` or `-dev.N` (N without leading zeros) | empty |
| `GLANCELOT_BUILD_KIND` | `release`, `rc`, `dev` or `local` | empty: `-rc.N` gives `rc`, `-dev.N` gives `dev`, no suffix gives `local`. A release is never inferred |
| `GLANCELOT_GIT_SHA` | the full 40- or 64-character commit SHA | empty: ask Git about this source tree |

**Rules, checked at configure time:**
- `release` and `local` take no suffix; `rc` needs `-rc.N` and `dev` needs `-dev.N`.
- `release` and `rc` fail when no SHA can be determined: no Git, not a checkout, or a source tree that is only a subdirectory of another repository (its host's SHA is never used).
- `dev` without a SHA warns and records `unknown`; `local` records `unknown` quietly.
- `SOURCE_DATE_EPOCH`, when set, fixes the build time, for reproducible builds.

**What the build does.** The values go into one generated translation unit, `build_info_values.cpp` in the build tree, never into a tracked source file. Every build re-reads `HEAD`, so a new commit needs no reconfigure. The file is rewritten only when the version, SHA, kind, `SOURCE_DATE_EPOCH` or its template change; then only that file is recompiled and the executables are relinked. The build time is taken when the file is written, so a fresh build has one build time. On Windows, `glancelot.exe` gets a VERSIONINFO resource: the numeric `X.Y.Z.0` file and product version, the full version string, and the prerelease flag for `rc` and `dev`.

**Commands:**

```sh
# Release (from the tagged commit, clean tree)
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLANCELOT_BUILD_KIND=release
# Release candidate (kind rc inferred)
cmake -S . -B build-rc -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLANCELOT_VERSION_SUFFIX=-rc.1
# A tree without .git (source archive)
cmake -S . -B build -G Ninja -DGLANCELOT_BUILD_KIND=release -DGLANCELOT_GIT_SHA=<full SHA>
```

On Windows, pass the same `-D` options to CMake.

**`--version`** prints four lines:

```text
Glancelot 0.1.0-rc.1 (0123456)
commit: 0123456789abcdef0123456789abcdef01234567
built: 2026-11-12T08:09:10Z
kind: rc
```

A build without a SHA prints `(unknown)` and `commit: unknown`. About shows the same values: the version in its heading, then the short commit (the full SHA is in its tooltip and accessible description), the build time and the kind. Values are selectable for copying.

**Docker.** The build context has no `.git`, so the commit and the label values come in as build args. `scripts/docker_build_args.py` prints them for the checkout: the version from `project()`, the suffix, the kind, `git rev-parse HEAD`, and one UTC instant, passed as both `SOURCE_DATE_EPOCH` and the created label. With `--image NAME` it also prints the `NAME:X.Y.Z` and `NAME:sha-<full SHA>` tags.

```sh
docker build $(python3 scripts/docker_build_args.py --kind release --image glancelot-browser) .
```

In PowerShell, write `@(python scripts/docker_build_args.py --suffix=-rc.1 --image glancelot-browser)` in place of `$(...)`. Write a suffix as `--suffix=-rc.N`, with the `=`.

The image gets the `org.opencontainers.image.version`, `revision` and `created` labels. The build stage fails if a given label arg disagrees with what the binary's `--version` reports. A plain `docker build .` still works: it is a `local` build with an `unknown` commit and empty labels.
