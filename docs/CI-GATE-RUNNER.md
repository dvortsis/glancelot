# Windows gate runner

Written for: the repository owner setting up the lead's Windows machine, and agent sessions that request gates.

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
