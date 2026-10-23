# Review round 2: lead scripts (gate lock, build slots, inbox)

Verdict: CHANGES REQUIRED

- Branch reviewed: `origin/claude/lead-scripts` at `10a676b`. Reviewed `git diff origin/main...origin/claude/lead-scripts`, with emphasis on the round-1 fixes in `git diff 43367a6 10a676b`.
- Reviewer: a fresh-context Claude Code reviewer (Opus) that did not write the branch or the first review. Ran on the Windows lead PC with PowerShell 7.6.6, as user `dvort` (not elevated).
- Date: October 22, 2026.
- Everything round 1 blocked on is fixed:
  - Exclusive claim: 0 overlaps in 50 synchronized gate races, and exactly one winner in each of 200 primitive races.
  - The two-slot-holder deadlock is gone.
  - A live slot older than `-StaleHours` now blocks a gate.
- **One new blocking finding (B1):** a child process of the gate holder that runs `build.ps1 -Test` recurses until PowerShell's call-depth limit. The fix is three lines, and a scratch copy with it passed.
- Three should-fix items remain (S1 to S3). None can make two gates overlap.
- S3 answers the lead's question. The pending marker does not fix gate-versus-gate starvation, because gate requests are not served in order.

All tests ran in a scratch git repo (`git init`) under the session scratchpad, using copies of the four scripts from `10a676b` and `origin/main`'s `gate-lock.ps1`. One driver-side `-Status` call (T4b, first attempt) ran with the real checkout as its current directory. `Show-LockStatus` only reads, so it changed nothing, but it printed the real lock: `held by dvort on lead/integrate-cycle1-a (pid 39468) since 21:54:26`. That is note N3 again, which is still open.

**Note for the lead.** That real integration gate (`lead/integrate-cycle1-a`) was held for the whole of this review, from about 21:54. The tests in this review ran until 22:38. This review put the PC under extra CPU load:

- busy-wait synchronized races;
- two call-depth recursions that spun for about 2 minutes each;
- a tight `Get-Content` reader loop.

If that gate failed a timing-budget test, treat the failure as possibly caused by this review and rerun the gate. No scratch processes were left running.

## Blocking finding

### B1. A child of the gate holder running `build.ps1 -Test` recurses until call-depth overflow

The new ancestor re-entry branch runs `& $Run; return` without setting `GLANCELOT_GATE_LOCK_HELD` to the child's PID. `build.ps1` relies on that variable to stop its own recursion: `if ($Test -and $env:GLANCELOT_GATE_LOCK_HELD -ne "$PID") { gate-lock -Run { & build.ps1 @args } }`.

In a child process, the cycle is:

1. `build.ps1` calls `gate-lock.ps1`.
2. `gate-lock.ps1` re-enters through the ancestor branch, with the variable still holding the parent's PID.
3. The `-Run` closure calls `build.ps1` again, and the check fails again.

- T6/T6d: `gate-lock -Run { pwsh -NoProfile -File build.ps1 -Test -BuildDir b-child }`. The child spun for about 2 minutes and exited with code 1: `The script failed due to call depth overflow.` (`gate-lock.ps1:13`). The `build.ps1` body never ran.
- Why this blocks:
  - Round 1 asked for this path (note N1). The header and `docs/CI-GATE-RUNNER.md` now promise it: "A child process started inside a gate's `-Run` block that calls `gate-lock.ps1` again runs without waiting."
  - `build.ps1 -Test` is the gate's main command.
  - On `main` this case waited 180 minutes and then failed, so the outcome is no worse. But the new code advertises the path and then crashes on it.
- Not affected: the CI workflow (in-process `& ./scripts/build.ps1`), the standalone `build.ps1 -Test`, and plain commands in a child (T6 6c and 6f re-entered in under 1 s).
- Verified fix, applied to a scratch copy (T6p): the child `build.ps1 -Test` exited 0 in under 1 s. Its body logged `held=<child pid>`, and the outer value was restored afterwards.

```diff
 if ($current -and $current.pid -and $current.host -eq $machine -and
-    ($ancestors.Contains([int]$current.pid) -or $env:GLANCELOT_GATE_LOCK_HELD -eq "$($current.pid)")) { & $Run; return }
+    ($ancestors.Contains([int]$current.pid) -or $env:GLANCELOT_GATE_LOCK_HELD -eq "$($current.pid)")) {
+    $outerHeld = $env:GLANCELOT_GATE_LOCK_HELD; $env:GLANCELOT_GATE_LOCK_HELD = "$PID"
+    try { & $Run } finally { if ($outerHeld) { $env:GLANCELOT_GATE_LOCK_HELD = $outerHeld } else { Remove-Item Env:GLANCELOT_GATE_LOCK_HELD -ErrorAction SilentlyContinue } }
+    return
+}
```

## Should fix

### S1. A reader that does not share Delete can make release leave `owner.json` behind, which stalls every gate and build until the holder's shell exits

`Close-LockFile` disposes the handle, then calls `Remove-Item ... -ErrorAction SilentlyContinue`.

- `Get-Content` opens files without `FileShare.Delete`. A separate test showed 34 of 300 deletes failing while another process looped `Get-Content` on the file.
- When the delete fails, `owner.json` stays with the holder's still-live PID.
- On this host, records no longer age out at `-StaleHours`. So every gate and every build slot waits until the holder process exits. On `main`, a leftover like this was cleared after `-StaleHours`.
- No overlap is possible. This is a stall that fails safe.
- T9b: a long-lived holder shell released while an old-style reader looped. The release left `owner.json (pid 21020)` behind. A new gate (Q) and a new build (B) both waited 45 s, until that shell exited, and then started.
- Who can trigger it:
  - an older `gate-lock.ps1` waiting or running `-Status` (its `Read-Owner` uses `Get-Content` every 30 s);
  - a person or agent running `cat` or `Get-Content` on the file;
  - possibly an indexer or antivirus scan.
- The script readers (`Read-LockJson`) share Delete and are safe. Slot and pending files share `Close-LockFile`, but only non-script readers touch them.
- The risk is real only when the holder is a long-lived interactive shell. The CI step, and a `pwsh` started for each tool call, exit soon after release, so their leftover goes stale at once.
- Suggested fix, in `Close-LockFile`:
  1. Truncate the file through the held handle (`$stream.SetLength(0); $stream.Flush()`) before `Dispose`.
  2. Retry the delete a few times.
  - An empty leftover then goes stale after 2 minutes through the existing no-record rule.

### S2. A gate holder's nested `build-slot.ps1` waits when every slot belongs to a requester blocked on that gate

- `Get-ForeignGate` returns `$null` for the caller's own gate, but `build-slot.ps1` still claims a slot afterwards.
- If every slot is held by a slot holder whose gate request is pending behind this gate, the claim cannot succeed. Those holders wait for the gate, and the gate's `-Run` waits for a slot.
- The wait lasts until `-TimeoutMinutes` (default 120), and then the gate's command fails.
- T2f (`-Slots 1`; the gate holder was made to win by releasing an older-script lock while it polled every 1 s and the slot holder every 20 s):
  - G took the gate.
  - Its nested `build-slot` failed after 1 minute: `No build slot after 1 minutes. Waiting: all 1 build slots held`.
  - Only then did A's gate start.
- This contradicts the doc: "A build started inside the gate holder's own tree … never waits."
- With the defaults it needs all three slots held by pending gate requesters, and a gate `-Run` that calls `build-slot.ps1`. Nothing in the repo does that today: `build.ps1` and `windows-gate.yml` never call it.
- Fix: when `Get-ForeignGate` finds that the gate belongs to the caller's tree, run `& $Run` without claiming a slot. The gate is already exclusive.

### S3. Gate requests are not served first come, first served, so a waiting gate can be starved by other gate users

The lead asked whether the pending-gate marker fixes the 57-minute wait seen in the first integration gate. It does not.

- What the marker does: it only stops new build slots while a gate waits.
- What it does not do: gate requesters are not ordered among themselves. Each waiter polls every `-PollSeconds`, which defaults to 30 and is what `build.ps1 -Test` uses. When the lock is released, it goes to whoever tries first:
  - a request that arrives just then claims the lock at once;
  - the holder's own process, if it asks again;
  - a waiter whose poll happens to fall in that gap.
- A gate that has waited a long time has no priority over these. This is the same as on `main`, so it is not a regression.
- T10:
  - Process B ran back-to-back 5-second gates, with 0.3 s between them, for 95 s (18 gates).
  - W requested a gate at 22:36:12 with the default poll.
  - W lost every release and started only at 22:38:12, after B stopped.
- Suggested fix: order the gates by their pending markers.
  - A requester claims `owner.json` only when no older live pending marker exists. "Older" means an earlier `started` time, with the PID breaking ties.
  - Requesters inside the current gate holder's tree are exempt, so re-entry is unaffected.
  - Shorten the gate's wait poll, for example to 5 s, so that the head of the queue claims promptly.
  - The stale-marker cleanup already keeps a crashed requester from blocking the queue.

## Non-blocking notes

- **N-a. Ancestor lists are trusted without checking for recycled PIDs.**
  - `Get-GateRequesterIds` uses the `ancestors` PID list recorded when the request was made.
  - Suppose a requester's ancestor exits while the request waits, for example a shell that used `Start-Process` and then returned. A new `pwsh` that reuses that PID and takes a slot is then ignored by every gate, and that build could overlap a gate.
  - This needs PID reuse to land exactly on a new slot holder, so it is unlikely.
  - Fix, either:
    - walk each live requester's ancestry at check time through `Process.Parent`, which rejects recycled parents;
    - or record `(pid, start time)` pairs.
- **N-b. A leftover `owner.json` makes its holder's later `gate-lock.ps1` calls re-enter without a claim.**
  - The holder's PID is in its own ancestor set, so after S1 the same shell's next `gate-lock.ps1` call takes the re-entry branch and runs without a claim.
  - This is still exclusive, because the leftover blocks everyone else, but it hides S1. Fixing S1 removes it.
- **N-c. An older `gate-lock.ps1` can lose to a new claim and then error out.** When it races the new script on an empty directory, its `Set-Content owner.json` can fail with a sharing violation and it throws.
  - Test 5f: an old script judged a live new holder stale (`-StaleHours 0`) and failed on `Remove-Item` with "being used by another process".
  - In both cases the old script fails without running. It never overlaps. The doc already says to run gates through the main checkout's script.
- **N-d. Remaining claim windows.**
  - A Ctrl+C that lands after `Open-LockFile` returns but before the assignment (`$pending = ...` or `$stream = ...`) can leak one handle in a long-lived shell.
  - The window is a few instructions long. Accept it.

## Round-1 items

| Round-1 item | Status at `10a676b` | Evidence |
|---|---|---|
| Blocking 1: gate claim not exclusive | Fixed. `owner.json` is created with `CreateNew` and the handle stays open. | Primitive race: 200/200 trials had exactly one winner (107/93) and 0 overlapping double wins. T1: 50 synchronized gate pairs, 0 overlaps, 0 errors, no leftovers. |
| Blocking 2: two slot holders requesting a gate deadlock | Fixed: requester ancestor trees are ignored. | T2: 2 holders, in-process. T2c: 3 holders, child-process requests. Both serialized; each later gate started after the previous holder's slot ended. An unrelated build waited for the pending markers to clear. |
| Should-fix 3: live slot past `-StaleHours` skipped | Fixed. Same-host records go stale only when the PID is dead or recycled. | T3: a gate with `-StaleHours 0` waited for a live slot. A slot with `-StaleHours 0` waited for a live gate. |
| N1: child of the gate holder waits on its parent | Fixed for plain commands, broken for `build.ps1 -Test` (B1). | T6 6c and 6f, versus T6d. |
| N2: claim loop outside `try` | Fixed. Write failures now give the claim back. | Code read; T7. |
| N3: lock directory depends on the current directory | Documented, not changed. | My own T4b `-Status` read the real lock. |
| N4: CI 240-minute timeout versus a 180-minute wait | Open. `windows-gate.yml` is unchanged on this branch. | Not applicable. |
| N5: stale directory removal without a re-read | Fixed. `owner.json` is deleted with the re-read guard, then the directory only if empty. | T4, T5c, T8. |
| N6: `build-slot.ps1` cleared rather than restored the variable | Fixed. | T7b: `env slot=[]` after the throw. |
| N7: `lead-inbox.ps1` read-only | Still read-only; unchanged since `43367a6`. Its only git commands are `fetch --prune`, `show`, `log`, `for-each-ref`, `ls-tree`, `cat-file` and `merge-base`, and it writes no files. | Code audit. |
| N9: `Process.Parent` and recycled parent PIDs | Evidenced by IL, not exercised. `ProcessCodeMethods.GetParent` calls `GetParentPid`, `Process.GetProcessById`, `get_StartTime` and `DateTime.op_GreaterThan`, which matches the start-time guard. | IL token scan. |

## Other checks that passed

- **Killed holders (T4, T4b, `Stop-Process -Force`).**
  - Killing a slot holder let the waiting gate start within 0.2 s.
  - Killing the gate holder made `-Status` report the lock as stale. A new build started at once, and the next gate removed the dead `owner.json` and ran.
  - Killing a pending requester, or a slot holder with its own pending request, did not block a new build.
  - No files were left over after any of these.
- **Recycled and dead PIDs (T8).** The test planted three records under the worker's own live PID, each with a `started` one hour before that process started: a pending marker, a slot and `owner.json`. It also planted a slot record with dead PID 999999. Each was removed with a `Removed stale …` warning, and the gate and the slot then ran.
- **`StartTime` access.** `StartTime` threw for 0 of 640 local processes. The runner (`Runner.Listener.exe`) runs as the same user, so `Test-LockStale`'s "live if `StartTime` throws" branch is not reached here.
- **Backward compatibility with `origin/main`'s `gate-lock.ps1` (T5).**
  - An old holder blocked both a new gate and a new build.
  - A new holder blocked an old waiter, and the old `-Status` command read the new `owner.json`.
  - A killed old holder was cleaned up by the new script, and a killed new holder by the old script.
  - Mixed race (T5e): 40 synchronized old-against-new pairs, with the old copy's poll shortened to 1 s. Result: 0 overlaps, 0 errors, no leftovers.
- **Re-entry (T6).**
  - `build.ps1 -Test` run standalone, the CI pattern (`$result.windows=True`) and `build.ps1 -Test` inside a build slot all ran their bodies with `held=<own pid>`.
  - An unrelated gate request that arrived during the gate (G9) waited and did not block the re-entrant children.
- **Ctrl+C (T7).** A real `CTRL_C_EVENT` was sent through `AttachConsole` and `GenerateConsoleCtrlEvent` to scratch workers. It hit four cases:
  - during gate `-Run`;
  - while a gate waited for a slot;
  - during `build-slot -Run`;
  - while `build-slot` waited for a gate.

  Each worker exited at once. Its own files were removed, so its `finally` blocks ran, and other processes' files stayed.
- **Throw and `PowerShell.Stop()` in a long-lived process (T7b).** A throw inside `-Run` left no files, and the variables were restored. `Stop()` during the run and `Stop()` during the wait left none of this process's files. An exclusive-open probe found no remaining handle.
- **Handle leaks.** After 30 gate cycles plus 30 slot cycles in one process, the handle count went from 535 to 541. That is not one handle per claim. 2000 `Read-LockJson` calls changed it from 542 to 542.

## Test output (scratch repo, trimmed)

```text
Primitive CreateNew race (Open-LockFile, 2 processes, synchronized ticks):
trials=200 a wins=107 b wins=93 overlapping double wins=0 sequential double wins=0 no winner=0

T1 end-to-end gate race, 2 processes x 50 iterations (hold 300 ms, -PollSeconds 1):
intervals=100 overlaps=0 unterminated=0   errors: 0
first g1=30 first g2=20 min gap ms=972 max gap ms=1494   leftover: []

T2 two slot holders each request a gate (-WaitMinutes 2):
22:00:20.731 A SLOT START / 22:00:20.817 B SLOT START
22:00:22.189 B GATE START / 22:00:22.650 driver launch C (plain build)
22:00:26.219 B GATE END / 22:00:28.254 B SLOT END
22:00:28.365 A GATE START / 22:00:32.393 A GATE END
22:00:33.304 C SLOT START / 22:00:34.456 A SLOT END   leftover: []

T2c three slot holders, gate requested from a child pwsh:
22:01:05.338 E GATE START (child) .. 22:01:08.372 END; 22:01:09.585 E SLOT END
22:01:10.438 A GATE START (child) .. 22:01:13.476 END; 22:01:14.701 A SLOT END
22:01:15.860 B GATE START (child) .. 22:01:18.894 END
22:01:19.881 C SLOT START (plain build launched 22:01:03.919)   leftover: []

T2f gate holder's nested build-slot, the only slot held by a pending requester (S2):
22:04:16.777 A SLOT START / 22:04:19.812 A request gate / 22:04:20.115 G request gate
22:04:24.910 G GATE START
22:05:26.116 G EXIT error: No build slot after 1 minutes. Waiting: all 1 build slots held
22:05:40.427 A GATE START

T3 live locks older than -StaleHours 0:
22:06:20.537 S1 SLOT START / 22:06:22.258 G1 request gate -StaleHours 0
22:06:28.555 S1 SLOT END / 22:06:28.965 G1 GATE START
22:06:31.162 S2 request slot -StaleHours 0 / 22:06:34.992 G1 GATE END / 22:06:35.650 S2 SLOT START

T4 kills:
22:07:00.093 KILL S1 (slot holder) -> 22:07:00.267 G1 GATE START; files after: []
22:07:36.625 KILL G2 (gate holder) -> status "gate lock: stale"; 22:07:38.347 S2 SLOT START; 22:07:40.813 G3 GATE START
22:07:17.973 KILL G4 (pending) and S4 (slot+pending) -> 22:07:18.901 S5 SLOT START (S3 still running)
final files: []

T5 compatibility with origin/main gate-lock.ps1:
5a 22:08:05.291 OLD GATE START .. 22:08:10.320 END -> 22:08:10.668 NEW GATE START -> 22:08:12.515 SLOT START
5b 22:08:14.738 NEW GATE START .. 22:08:49.768 END -> 22:09:16.267 OLD GATE START (30 s poll)
   old -Status while new holds: holder dvort ... pid 25268 ... branch N6
5c KILL O3 -> 22:09:20.018 NEW GATE START; files: []
5d KILL N4 -> 22:09:23.318 OLD GATE START; files: []
5f old -StaleHours 0 vs live new holder: O5 EXIT error: The process cannot access the file '...owner.json' because it is being used by another process.
5e mixed race 40 x old/new: intervals=80 overlaps=0 unterminated=0, starts new=40 old=40, leftover: []

T6 re-entry:
22:12:17.674 build.ps1 body pid=10040 Test=True BuildDir=b-standalone held=10040
22:12:17.851 build.ps1 body pid=10040 Test=True BuildDir=build-ci held=10040; 6b result.windows=True
22:12:17.853 6c child pwsh calls gate-lock -> 22:12:18.467 child exit 0
22:13:55.091 6f grandchild (cmd -> pwsh) calls gate-lock -> 22:13:55.777 exit 0
22:13:58.262 build.ps1 body pid=10040 Test=True BuildDir=b-slot held=10040
T6d (B1):
22:14:21.559 GATE START
22:16:24.937 child exit 1: The script failed due to call depth overflow. (gate-lock.ps1:13) -- build.ps1 body never ran
T6p (patched scratch copy):
22:25:44.037 build.ps1 body pid=24436 Test=True BuildDir=b-child held=24436; child build.ps1 -Test exit 0
child gate-lock: child re-entered, held=18536 pid=18536 / after: held=40812

T7 real Ctrl+C (GenerateConsoleCtrlEvent):
gate -Run:         files before: glancelot-gate-lock -> after: []
gate waiting:      before: build-slot-1, gate-pending-26668 -> after: build-slot-1 (the other process's)
build-slot -Run:   before: slot-1, slot-2 -> after: slot-1 (the other process's)
build-slot waiting on a gate: before: glancelot-gate-lock -> after: glancelot-gate-lock (the gate's own)
T7b: caught: boom; files after throw: []; env held=[]   caught: boom2; files after: []; env slot=[]
     handles before 30 gate+slot cycles=535 after=541; Read-LockJson x2000 handles 542 -> 542
     [run] after Stop: []   [wait] after Stop: [glancelot-build-slot-1.json] (the blocker's); probe: no held gate files

T8 recycled and dead PIDs:
Removed stale glancelot-gate-pending-42564.json / glancelot-build-slot-1.json / owner.json (pid 42564, started 1 h before the process)
GATE START with planted records; after: []
Removed stale glancelot-build-slot-1.json (recycled) / glancelot-build-slot-2.json (pid 999999); SLOT START; after: []

T9b old-style reader during release (S1):
22:20:27.061 P cycle 1 released; owner.json LEFT BEHIND with pid 21020
22:20:27.880 B request build slot / 22:20:27.925 Q request gate
22:21:12.089 P shell exits -> 22:21:13.278 Q GATE START -> 22:21:14.072 B SLOT START

T10 gate-vs-gate starvation (S3):
22:36:12.139 W request gate (default -PollSeconds 30)
22:37:48.998 B stopped after 18 back-to-back gates
22:38:12.626 W GATE START
```
