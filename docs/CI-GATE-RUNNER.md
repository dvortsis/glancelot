# Windows gate runner

Written for: the repository owner setting up the lead's Windows machine, and agent sessions that request gates.

When the lead runs locally on this PC, its local tester runs the full gates directly and this runner is the backup (AGENTS.md, local lead topology). When the lead runs in the cloud, this runner is the gate.

Cloud sessions cannot run `scripts/build.ps1 -Test` themselves. To get a real Windows gate, they push a branch named `gate/<name>`. The `windows-gate` workflow (`.github/workflows/windows-gate.yml`) then runs on this machine as a self-hosted runner:

1. It creates a temporary worktree of `C:\src\Github\Glancelot` at the pushed commit.
2. Under the machine gate lock, it runs the full Windows gate and then the Docker gate.
3. It writes a pass/fail summary, uploads the logs, and removes the worktree.

The workflow refuses any commit that does not contain merge `334065e` (the Windows native fixture hold).

The same push also starts the **hosted-windows (advisory)** job on a GitHub-hosted `windows-2022` VM. That VM is free for public repositories and runs even when this machine is off. The job installs Qt 6.8.3 with the Dockerfile's archives (cached between runs), builds with MSVC and runs the full CTest suite.

The hosted job only catches MSVC compile errors and most test failures early. It never qualifies a packet:

- it has no GPU (it renders with the WARP software rasterizer);
- its timing is noisy on 4 shared vCPUs, so the budget tests flake there;
- its native desktop session is not guaranteed.

A failure seen only on the hosted job is triaged against the authoritative **lead-windows-gate** job before anyone acts on it.

## One-time setup (owner, on this machine)

The repository is public, so these steps are done by hand.

1. **Lock down fork workflows.** In GitHub, open the repository's *Settings → Actions → General*. Under *Approval for running fork pull request workflows from contributors*, choose **Require approval for all external contributors**. Never add a `pull_request` trigger to `windows-gate.yml`: pull-request workflows would run untrusted code on this PC.
2. **Install the runner.**
   1. Open *Settings → Actions → Runners → New self-hosted runner → Windows x64*.
   2. In a new empty folder, `C:\actions-runner-glancelot`, run the download, hash-check and extract commands that page shows.
   3. Configure:

      ```powershell
      cd C:\actions-runner-glancelot
      .\config.cmd --url https://github.com/dvortsis/glancelot --token <token from that page> `
        --name MONDOSHAWAN-glancelot-gate --labels glancelot-gate --work _work --unattended
      ```

      Do not install it as a service. The native UI tests need your interactive desktop session.
3. **Start it with your desktop session.** Run `C:\actions-runner-glancelot\run.cmd` now. To start it at every logon:

   ```powershell
   $action  = New-ScheduledTaskAction -Execute 'C:\actions-runner-glancelot\run.cmd' -WorkingDirectory 'C:\actions-runner-glancelot'
   $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
   Register-ScheduledTask -TaskName 'Glancelot gate runner' -Action $action -Trigger $trigger -RunLevel Limited
   ```

4. **Keep Docker Desktop running** if gates should include the Linux Docker build.
5. **Smoke test:**

   ```powershell
   gh workflow run windows-gate.yml --repo dvortsis/glancelot --ref main -f docker=false
   ```

   It should appear as a run, take the gate lock (check it with `scripts/gate-lock.ps1 -Status`) and finish with a summary.

While a gate runs, avoid starting other full gates or heavy builds on this machine. Several tests have wall-clock budgets.

## Requesting a gate (agents)

1. Push the exact commit to be gated as `gate/<task>-<n>`, for example `git push origin HEAD:refs/heads/gate/stack-correction-1`.
2. Find and follow the run: `gh run list --repo dvortsis/glancelot --workflow windows-gate.yml --branch gate/<task>-<n>`, then `gh run watch <id>`.
3. Record in the evidence:
   - the run URL;
   - the summary;
   - the downloaded logs (`gh run download <id>`).
4. A run queued with no runner online means this machine is off. Report the gate as pending; do not mark the task done.
5. Delete the `gate/*` branch after its result is recorded.

## Local capacity and inbox (lead)

Two read-mostly helpers for the lead on this PC, both run from the main checkout in one PowerShell 7 process.

**Build slots.** `scripts/build-slot.ps1` caps local builds at three at once and keeps builds and full gates apart.

```powershell
& C:\src\Github\Glancelot\scripts\build-slot.ps1 -Branch claude/x -Run { cmake --build build-claude-x }
& C:\src\Github\Glancelot\scripts\build-slot.ps1 -Status
```

- Each slot is a file `glancelot-build-slot-<n>.json` in the git common dir of the current directory's repository (`git rev-parse --git-common-dir`). It records pid, host, branch, cwd and start time. Run the scripts from inside the Glancelot checkout or one of its worktrees, or they use another repository's lock area.
- Every claim (slot, gate ticket, gate `owner.json`) is an exclusive file create (`FileMode.CreateNew`). The holder keeps the file open until it releases, so exactly one process wins a claim and, on Windows, a live holder's file cannot be deleted. On release the holder empties the file through its handle, closes it and deletes it, retrying while another reader has it open.
- `-Run` waits for a free slot, up to `-TimeoutMinutes` (default 120). A claim is stale when its holder process is dead, its PID now belongs to a later process, or nobody holds the claim file open any more (a leftover from a release); a live build keeps its slot however long it runs. `-StaleHours` (default 6) applies only to records from another host. A stale file is removed only if a re-read still shows the record judged stale. The scripts never kill processes.
- A full gate and a build never overlap, in either direction:
  - `-Run` waits while another process tree holds the gate lock or has a gate request pending.
  - `gate-lock.ps1 -Run` first takes a ticket, `glancelot-gate-pending-<ticket>.json`, so no new slot starts while it waits. Requests are served first come, first served: only the lowest live ticket may claim the gate (a crashed requester's ticket is skipped), so a waiting gate is not starved by later requests. The head of the queue then waits ("Waiting: N build slots held", within `-WaitMinutes`, polling every `-PollSeconds`, default 5) until no other process tree holds a slot, takes the lock, and removes its ticket.
  - An older `gate-lock.ps1` without tickets still competes for a free lock, but never overlaps a gate.
  - A live but stalled request at the head of the queue holds everything behind it until its process exits; `-Status` shows its ticket, PID and branch. The scripts never kill processes.
  - The gate is claimed by creating `glancelot-gate-lock\owner.json` exclusively. The directory is kept, so an older `gate-lock.ps1` that claims the directory still sees the lock, and a lock held by an older copy is honoured.
  - Pending markers are stale-safe like slots: a crashed gate request does not block builds.
- Nothing deadlocks on itself:
  - A gate does not wait for slots held by its own process tree, or by a tree that is itself waiting for or holding the gate (that holder is blocked, not building). So a reviewer that holds a slot can call `gate-lock.ps1` for its targeted tests, and two slot holders that both request a gate take the gate one after the other.
  - A child or grandchild process started inside a gate's `-Run` block that calls `gate-lock.ps1` again (for example `pwsh -File scripts/build.ps1 -Test`) runs without waiting and sets `GLANCELOT_GATE_LOCK_HELD` to its own PID, so `build.ps1` does not recurse.
  - A build started inside the gate holder's own tree (same process, an ancestor, or a child that inherits `GLANCELOT_GATE_LOCK_HELD`) runs at once without taking a slot.
  - The gate lock itself is never removed by `build-slot.ps1`.
- Nested calls in the same process, or in a child of the slot holder, reuse the slot (`GLANCELOT_BUILD_SLOT_HELD`).
- Use one slot count for every caller: `-Slots` (default 3) or `$env:GLANCELOT_BUILD_SLOTS`. A caller only looks at slots 1 to N.
- `gate-lock.ps1 -Status` and `build-slot.ps1 -Status` both show the gate lock, queued gate tickets and every slot; stale files are listed but not counted. The shared lock code is `scripts/lock-common.ps1`.
- A gate run through an older checkout's `gate-lock.ps1` does not know about slots. Run gates through the main checkout's script.

**Inbox.** `scripts/lead-inbox.ps1` fetches with `--prune` and prints one row per `origin/claude/*` packet branch. It is read-only.

- Each row shows: tip SHA and date, task id, queue state on `origin/main`, merged into `origin/main` or not, the handoff, the latest "Review round" heading, and each `origin/claude/review-<task>[-*]` branch with the verdict from its newest review file.
- The queue row is matched by its `branch:` field. Rows without one are matched by the id parsed from the branch name and marked `*`.
- The handoff is the newest `docs/handoff/*-HANDOFF.md` that the branch's own commits touched, else `<TASK>-HANDOFF.md`.
- A review branch that matches no packet gets its own row. `-Json` gives machine output, `-NoFetch` skips the fetch.