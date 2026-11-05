# Keep child slots full, then pause on November 5

Written for: the lead.

**Decision (user, November 5, 2026).** Three rules for the lead, which plays the role of the Lodestar build runner here (docs/decisions/2026-11-04-merge-protections.md). A Lodestar "slice" is a packet here, and a worker slot is one of the four child-session slots in AGENTS.md, "Capacity".

1. **Fill free slots.** Start the next ready packet whenever a child slot is free, unless the lead records a reason. Only three reasons count:
   - *File conflict:* the packet would edit files a running packet owns or has been granted (hot files, grants, track ownership).
   - *Release branch:* the packet has to wait for a release-branch step. No release branch exists yet (F6 REL-01).
   - *CI queue:* the gate queue or `ci-ok` backlog is full, or the local build-slot and gate-lock limits leave no room for another build.

   Record each held slot where the user can see it: the packet's `blocked_by` in `docs/WORK-QUEUE.yaml` (in the next lead bookkeeping PR) and the lead's status report. Name the reason and what the packet is waiting on, such as the running packet and the files, or the gate that has to finish.
2. **Hourly check.** Set a recurring one-hour timer. Each time it fires, check that every slot is busy while unblocked ready work exists, and start the next ready packet in any free slot that has no recorded reason. If nothing changed, re-arm silently.
3. **Pause at 5:00 pm Mountain time on Thursday, November 5, 2026** (2026-11-06 00:00 UTC). From then on, start no new packets, including follow-ups and fix packets for new work. Let the running packets finish through review, gate and merge, and fix rounds on them are allowed. When none is running, cancel the hourly timer and record the pause the way `docs/handoff/LEAD-PAUSE-2026-10-23.md` did: the packets finished, any open PR and why it's open, and anything waiting on the user. Then wait for the user to resume.

**Why.** The user wants no idle capacity while there is unblocked work, a visible reason for every slot that sits idle, and a clean stopping point tomorrow evening.
