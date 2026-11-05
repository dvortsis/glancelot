# Keep every slot busy

Written for: the lead and anyone reading why a ready slice is not running.

**Decision (user, November 4, 2026):** launch the next ready slice whenever a slot is free, unless the lead records a reason. The reasons are a file conflict with a running slice, the release branch, or the CI queue. The lead also sets a one-hour timer to check that all slots are busy if there is unblocked work.

**How it works here**

- A slot is one running child session or subagent, reviewers and the gate tester included. There are four.
- Holds live in `slot_holds` in `docs/WORK-QUEUE.yaml`, with the slice and the reason, so a stopped slice can always be explained. A lead bookkeeping PR changes them, and the hold is removed when the reason ends.
- Unmet dependencies do not need a hold, because the slice is not ready yet.
- A timer lives only in the lead's session and expires after seven days. The lead sets it again on resume and after expiry.
- The desktop app refuses to start a session from a timer or other automated turn. On those turns the lead launches the slice as a background subagent in its own worktree, which uses a slot the same way.
