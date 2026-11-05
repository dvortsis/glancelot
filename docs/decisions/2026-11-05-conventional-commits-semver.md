# Conventional Commits and semantic versions

Written for: every session that commits to Glancelot.

**Decision (user, November 5, 2026; rules by the lead):** commits and PR titles follow Conventional Commits, and releases follow Semantic Versioning, as `docs/RELEASING.md` describes. `guard` enforces the format for commits committed on or after November 6, 2026, and for PR titles from PR 19 on. A `commit-msg` hook gives the same check locally.

**Why:** the history mixed `LEAD:`, `<TASK-ID>:` and Conventional subjects, so nothing could derive a version or release notes from it. Typed commits make the version effect of each change visible at review time.

**What does not change:** no history is rewritten. Shared branches are never rebased or force-pushed (AGENTS.md), and commits before the cutoff are exempt. Merge commits keep git's subject. The task stays traceable through a `Task:` trailer and the PR title's suffix.

**Not yet done:** build provenance (version, full SHA, build time, build kind in About and `--version`; OCI labels and SHA tags on the Docker image) is queue row REL-01, not started while new slices are paused. No release is made until it lands. The version stays 0.1.0 until the first release.
