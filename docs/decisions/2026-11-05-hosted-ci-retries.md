# Hosted CI: budget tests advisory, one retry

Written for: the lead and anyone reading a `viewer` result.

**Decision (lead, November 5, 2026):** in the hosted `viewer` job, tests labelled `budget` run in their own step and cannot fail the job, and every other test gets one retry (`--repeat until-pass:2`). The local full gate is unchanged: no retries, and budget tests count.

**Why:** PR 5's head failed `viewer` twice, on different tests each time (`fs_watch`, then `fs_watch_budget`, `recovery_controller`, `tabs_recovery` and `monitor_customization_ui`), while the same commit passed the full gate on Windows and Docker, and PR 5 changes no code those tests run. `viewer` feeds the required `ci-ok`, so runner timing noise was blocking merges. Timing budgets do not mean anything on a shared 4-vCPU runner.

**Cost:** a real intermittent failure can pass hosted CI on its retry, and a budget regression shows up only at the full gate. The retried and advisory results stay in the job's log, and the full gate still decides done.
