# Review: lead scripts (gate lock, build slots, inbox)

Verdict: CHANGES REQUIRED

- Branch reviewed: `origin/claude/lead-scripts` at `43367a6`, diffed against `origin/main` (`4f29124`).
- Reviewer: a fresh-context Claude Code reviewer (Opus) that did not write this branch, on the Windows lead PC, using PowerShell 7.6.6.
- Date: October 22, 2026.
- Scope: `scripts/gate-lock.ps1`, `scripts/lock-common.ps1`, `scripts/build-slot.ps1`, `scripts/lead-inbox.ps1` and the new section in `docs/CI-GATE-RUNNER.md`.
- Blocking findings: 1 and 2. Finding 3 is a one-line fix that should land with them.

All lock tests ran in a scratch git repo under the session scratchpad, using copies of the scripts. The real lock files were never written. One driver-side `-Status` call accidentally ran with the real checkout as its cwd. `Show-LockStatus` only reads, so that call changed nothing; it printed the real gate lock (`held by dvort on review/en-02`). It also demonstrates note N3 below.

## Blocking findings

### 1. The gate lock is not exclusive: two gates can run at once

The gate lock is still claimed with `New-Item -ItemType Directory -Path $lock -ErrorAction Stop`. In PowerShell 7 this call checks whether the directory exists and then calls `CreateDirectory`, which succeeds when the directory already exists. Two processes that try at the same moment can both succeed.

- Direct race test: two processes created the same directory 40 times, synchronized to the millisecond. In **18 of 40** trials both calls succeeded.
- End-to-end (T1): two `gate-lock.ps1 -Run` gates started at the same instant. Both printed `GATE START` 3 ms apart and ran together for the whole 6 seconds.
  - Both then wrote `owner.json` with `Set-Content`, so the file held two concatenated JSON objects and could not be parsed.
  - Neither `finally` block recognized itself as the owner, so the lock directory was left behind. It was cleared only by the 2-minute no-owner stale rule.
- The flaw already existed on `origin/main`, but this branch rewrites the claim loop, and the doc and the header still call the claim atomic. Gates that wait for slots now also tend to become ready together, as soon as the last slot is released.
- Fix: keep the directory for older checkouts, but decide ownership with an exclusive file claim. Create the directory with `-Force`, then call `[IO.File]::Open($ownerFile, [IO.FileMode]::CreateNew, ...)` and write the record through that handle. The loser treats the lock as held.
  - Holding the handle open for the gate's lifetime, as the slot files already do, also stops a waiter from deleting a live gate's lock.
  - Remove the lock with the same re-read guard that `Remove-StaleLockFile` uses. That also closes note N5.

### 2. Two slot holders that each request a gate deadlock for the full `-WaitMinutes`

Each gate ignores only the slots held by its own process tree. When A and B each hold a slot and then call `gate-lock.ps1` (or `build.ps1 -Test`), A waits for B's slot and B waits for A's.

- While they wait, both pending markers block every new build on the machine.
- T5 used `-WaitMinutes 1`. Both gates failed after one minute with `No gate after 1 minutes. Waiting: 1 build slots held (<the other>)`. An unrelated build, C, started at 21:24:30 and waited until both gates had failed at 21:25:26.
- With the defaults this is 180 minutes of a stalled machine, and then two failed gates.
- The new doc section recommends exactly this pattern: "a reviewer that holds a slot can call `gate-lock.ps1`". `build-slot -Run { build.ps1 -Test }` is the natural way for an agent to use the new script.
- Fix, one of:
  - (a) Record the requester's ancestor PIDs in the pending marker. Then the gate's slot check ignores any slot whose holder PID appears in another live pending marker's ancestor list, because that holder is blocked inside its own gate request. The gate directory still serializes the gates, and after A's gate, B waits only until A releases its slot.
  - (b) Make `gate-lock.ps1` refuse at once when its caller holds a slot, and remove the doc claim. This makes `build.ps1 -Test` unusable inside a slot.
  - Option (a) is preferred.

## Should fix

### 3. A live slot older than `-StaleHours` does not stop a gate

`Get-LiveLocks` calls `[void](Remove-StaleLockFile ...); continue` for any record that `Test-LockStale` calls stale. A live holder keeps its file open, so the delete fails, but the slot is skipped anyway. A gate therefore starts beside a build that has been running longer than `-StaleHours`.

- T6 simulated this with a gate run with `-StaleHours 0`. The gate started 2 seconds after the slot, while the slot was still running.
- This contradicts the doc: "a build that runs longer than `-StaleHours` keeps its slot" and "never overlap".
- Fix: call `continue` only when `Remove-StaleLockFile` returns `$true`, and otherwise report the slot as live.
- `Get-GateOwner` has the same blind spot in the other direction. A live gate older than `-StaleHours` returns `$null`, so builds start beside it. Consider applying the age limit only when the PID cannot be checked.

## Non-blocking notes

- **N1. A child process re-entering the gate waits on its own parent.** Gate re-entry matches only `GLANCELOT_GATE_LOCK_HELD -eq $PID`. A child `pwsh` started inside a gate that runs `build.ps1 -Test` waits on its own parent until `-WaitMinutes` expires (T9).
  - This already happened on `main`. Now the child's pending marker also blocks every build on the machine for that time.
  - Suggest re-entering when the gate owner's PID is among the caller's ancestors, or when it equals the inherited `GLANCELOT_GATE_LOCK_HELD`.
- **N2. The `build-slot.ps1` claim loop sits outside `try/finally`.**
  - An exception or Ctrl+C after `Open-LockFile` and before `try`, during the re-check `Get-ForeignGate`, leaks the open stream.
  - In a long-lived interactive shell the PID stays alive, so the slot stays held until the shell exits.
  - In `Open-LockFile`, a write failure after `CreateNew` also leaks the handle.
- **N3. The lock directory depends on the current directory, not the script.** `lock-common.ps1` finds the common dir with `git rev-parse` from the current directory. Running the main checkout's script from another repository or clone uses that repository's `.git`, and the two never see each other's locks.
  - This was already true on `main`. The CI path is safe because `GATE_TREE` is a worktree of the main checkout.
  - Either derive the directory from `$PSScriptRoot` or document the requirement.
- **N4. CI timeout budget.** `windows-gate.yml` keeps `timeout-minutes: 240`. The gate's 180-minute `-WaitMinutes` now also covers waiting for build slots, so a gate that waits a long time can be killed by the job timeout partway through the build.
- **N5. Stale gate-directory removal has no re-read guard.** It still uses `Remove-Item -Recurse -Force`, so a waiter can remove a gate that another waiter has just claimed. This already happened on `main`; the fix for finding 1 closes it.
- **N6. `build-slot.ps1` clears the variable instead of restoring it.** Its `finally` removes `GLANCELOT_BUILD_SLOT_HELD`. When an inherited, non-ancestor value was present before the call, that value should be restored instead.
- **N7. `lead-inbox.ps1` is read-only.**
  - The only git commands it runs are `fetch`, `for-each-ref`, `log`, `show`, `ls-tree`, `cat-file` and `merge-base`. It has no file writes.
  - `-NoFetch` on the real repo left every ref unchanged (`refs unchanged: True`) and took 17 s.
  - `fetch --prune` changes only remote-tracking refs.
  - `Get-TaskKey` requires a digit right after the first word, so `claude/copy-safety-01` and `claude/lead-scripts` get no task id. Their review branches, including this one, show up as orphan rows unless the queue row has a `branch:` field.
- **N8. The `-Status` text changed.** It now prints `gate lock: free` instead of `gate lock free`. Nothing in the repo parses it; only docs mention `-Status`.
- **N9. Ancestry walk and recycled parent PIDs.** `Get-AncestorIds` walks `Process.Parent`. As I remember the PowerShell 7 source, `ProcessCodeMethods.GetParent` rejects a parent whose start time is later than the child's, which would guard against a recycled parent PID. I did not check the source in this review or exercise the case, so treat this as **unverified**.

## Verified working

- **Exclusive file claims.** Slot and pending files are claimed with `FileMode.CreateNew`, and a second `CreateNew` returns `$null`.
  - While the holder keeps the file open, readers can read it and `File.Delete` fails with `IOException`.
  - `Remove-StaleLockFile` refuses to delete when the re-read record does not match the record judged stale.
- **Stale detection.** `started` comes back from JSON as a local-kind `DateTime` (`age=0.015s`). `Test-LockStale` results:

  | Record | Result |
  |---|---|
  | Live PID | live |
  | Live PID started after the claim (recycled) | stale |
  | Dead PID | stale |
  | Older than `-StaleHours` | stale |
  | Other host | live |

- **Gate and slot ordering.** The claim-then-recheck order is sound. A slot writes its file, then reads the gate and pending files. A gate writes its pending marker, then reads the slots, claims the lock, and reads the slots again. A file that is claimed but still empty counts as live for 2 minutes.
- **Gate waits for a slot (T2).** The gate waited (`Waiting: 1 build slots held (s1 pid ...)`) and started 1 s after the slot ended.
- **Pending gate blocks a new slot (T3).** S2 printed `Waiting: 1 gate request(s) pending`, then `Waiting: full gate held`, and started only after the gate ended.
- **Self-held slot (T4).** A gate called in the same process, a gate called from a child `pwsh`, a nested `build-slot` inside the gate, and nested `build-slot` re-entry all ran without waiting. No files were left over.
- **Kill -9 (T7).** Each kill used `Stop-Process -Force` on a scratch process.
  - Killing the slot holder let the waiting gate start within 1 s.
  - Killing the gate holder left the lock reported as stale. A new build started at once, and the next gate replaced the lock.
  - Killing a pending gate did not block a new build. No files were left over.
- **`build.ps1` re-entrancy (T8).** The scratch copy of `build.ps1` kept the first 8 lines of `main` unchanged and replaced the body with a log line. The standalone call, the CI pattern (outer `gate-lock -Run` that sets `$result` through dynamic scope) and the call inside a build slot all ran serialized, one gate each. The CI block reported `result.windows=True`.
- **Error paths (T10).** A `throw` inside `-Run` and `PowerShell.Stop()` both released every lock file, whether the stop came during `-Run` or during the wait. `Stop()` is the pipeline-stop mechanism that Ctrl+C uses.

## Test output (scratch repo, trimmed)

```text
Direct race, New-Item -ItemType Directory, 2 processes:
trials=40  proc1 wins=29 proc2 wins=29  BOTH succeeded creating the same directory in 18 trials

T1 two concurrent gates (new gate-lock.ps1):
21:18:42.209 G1 pid=28492 GATE START
21:18:42.212 G2 pid=45632 GATE START
21:18:48.225 G1 pid=28492 GATE END
21:18:48.225 G2 pid=45632 GATE END
leftover: glancelot-gate-lock   (owner.json held two concatenated JSON records)

T2 gate waits for a sibling's slot:
21:23:32.750 S1 SLOT START / 21:23:40.781 S1 SLOT END / 21:23:41.791 G1 GATE START

T3 pending gate blocks a new slot:
21:23:44.999 S1 SLOT START; 21:23:46.664 G1 launch; 21:23:48.632 S2 launch
21:23:53.025 S1 SLOT END; 21:23:53.281 G1 GATE START; 21:23:57.302 G1 GATE END; 21:23:58.293 S2 SLOT START

T4 self-held slot:
21:24:16.436 A SLOT START
21:24:16.851 A GATE START (same process)
21:24:17.094 A nested slot inside gate: no wait
21:24:19.373 child GATE START (child process of slot holder)
21:24:20.723 A nested build-slot re-entry: env=25020
slot files left: 0

T5 two slot holders each request a gate (-WaitMinutes 1):
21:24:22.068 A SLOT START / 21:24:22.195 B SLOT START / 21:24:30.149 C launch
21:25:26.456 A exit error: No gate after 1 minutes. Waiting: 1 build slots held (b pid 7420)
21:25:26.490 B exit error: No gate after 1 minutes. Waiting: 1 build slots held (a pid 22484)
21:25:26.684 C SLOT START (unrelated build)

T6 live slot past -StaleHours (gate -StaleHours 0):
21:25:46.177 S1 SLOT START / 21:25:48.340 G1 GATE START / 21:25:56.200 S1 SLOT END

T7 kill -9:
21:26:01.603 driver KILL S1 -> 21:26:02.507 G1 GATE START
21:26:06.794 driver KILL G1 (gate holder) -> status "stale"; 21:26:09.239 S1 SLOT START; 21:26:10.701 G2 GATE START
21:26:16.025 driver KILL pending G1 -> 21:26:17.603 S2 SLOT START (S1 still running)
leftover: (none)

T8 build.ps1 re-entrancy:
21:26:38.497 build.ps1 body pid=30768 Test=True BuildDir=build-ci held=30768
21:26:40.583 CI result.windows=True seconds=2
21:27:08.854 build.ps1 body pid=43764 Test=True BuildDir=b-a held=43764
21:27:11.470 build.ps1 body pid=47452 Test=True BuildDir=b-sl held=47452
leftover: (none)

T9 child pwsh of the gate holder calls gate-lock:
21:27:29.704 X GATE START
21:28:31.898 X child exit code 1   ("Waiting for gate lock held by dvort on x")

T10 release on error and Stop():
caught: boom; after throw, lock files: (none)
[run]  before Stop: glancelot-gate-lock,glancelot-build-slot-1.json   after Stop: (none)
[wait] before Stop: slot-1, slot-2, gate-pending-46892                after Stop: slot-1 (the other process's)
```
