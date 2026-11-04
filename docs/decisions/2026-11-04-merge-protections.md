# Merge protections

Written for: the lead and every session that opens a pull request.

**Decision (user, November 4, 2026):** adopt the branch, merge and collision safeguards used on the Lodestar v2 repository, then restart the build with up to four child sessions at a time. The working rules are in AGENTS.md, "Pull requests and merging".

**How each safeguard maps to Glancelot**

| Lodestar | Glancelot |
| --- | --- |
| No direct pushes to `main` | Branch protection, administrators included, because every session uses the owner's credentials |
| Build runner merges, then auto-merge | The lead merges with `scripts/lead-merge.ps1`, which can fall back to auto-merge |
| Required `ci-ok` | `ci-ok` in `build.yml`, not required to be up to date with `main` |
| Fresh Opus reviewer | `fresh-review` commit status, checked by `lead-merge.ps1` |
| Known red check does not block | `ci-known-red` label plus a PR comment, lead only |
| `slices.yaml`, `dashboard.yaml` | `docs/WORK-QUEUE.yaml`; `pr_guard.py` limits a packet to its own row |
| `runner-state` worker slots | Gate lock and build slots in the shared git directory (already in place) |
| `slice/<id>` | `claude/<task>`, which `lead-inbox.ps1` and existing packets already use |
| `cargo` and `npm` checks | `pr_guard.py`, `ctest`, catalog regeneration |

**Added for Glancelot**

- `local-gate` commit status. The full Windows and Docker gate on the user's PC still decides done, because hosted CI has no GPU and no Linux viewer build.
- `lead-grant` label. Packets cannot change `.github/workflows/*` or `scripts/ci/*` without it, so a session cannot relax its own check.

**Exceptions to the Lodestar rule**

- Lead bookkeeping PRs (only the queue, handoffs, decisions or evidence) need `ci-ok` but no reviewer. A fresh review of each evidence commit would cost more than it finds.
- The `fresh-review` and `local-gate` statuses are checked by `lead-merge.ps1`, not by branch protection, because a required status would also block docs-only PRs. Every session shares the owner's credentials, so these two checks rest on the sessions following the process.
