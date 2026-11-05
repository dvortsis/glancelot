# Full gate only on the head about to merge

Written for: the lead and every implementer and reviewer.

**Decision (lead, November 4, 2026, under the user's instruction to make decisions that keep the work moving):** implementers and reviewers run targeted tests only. The PR's hosted `viewer` job is their full-suite check. The full Windows and Docker gate runs on the exact head to be merged, by the tester.

**Why:** the gate lock serves one run at a time, and a full gate takes 30 to 40 minutes. With four slots, sessions spent hours queueing behind each other's full suites, while the merge head still needed its own gate. A reviewer's targeted test was skipped because it sat behind those gates.

**What does not change:** the full gate still decides done, on the exact head, before merge. Budget tests and measurements that need a quiet machine still take the gate lock.

**Cost:** a Linux-only failure now shows up at the Docker gate, as `layout_presets_ui` did in cycle1-e, because hosted CI has no Linux viewer build. A Linux viewer job in CI would close this gap.
