# Review round 3: lead scripts (gate lock, build slots, inbox)

Verdict: CHANGES REQUIRED. One line is left to fix. With that fix applied, this reviewer approves the branch.

- **Branch reviewed:** `origin/claude/lead-scripts` at `f9ff6d8`.
  - Reviewed `git diff origin/main...origin/claude/lead-scripts`.
  - Focused on the round-2 fixes in `git diff 10a676b f9ff6d8`: B1 child re-entry, S1 stranded `owner.json`, S2 the gate's nested build slot and S3 FIFO tickets.
- **Reviewer:** a fresh-context Claude Code reviewer (Opus). It did not write the branch or either earlier review. It ran on the Windows lead PC under PowerShell 7.6.6 as user `dvort`, not elevated.
- **Date:** October 22, 2026.
- **Round-2 status:** all round-2 items are fixed and tested. B1, S1, S2 and S3 each have evidence in the table below.
- **Main results:**
  - 0 overlaps in a synchronized 40-iteration race between two new gates.
  - 0 overlaps in a 40-iteration race between the new gate and the old one.
  - Gates were served in first-come order, and crashed tickets were skipped.
  - No deadlocks and no recursion.
  - Every release path left no files behind.
- **One blocking finding (B1).** `Remove-StaleGate` re-reads `owner.json` and passes that fresh record as the "judged stale" record, so its re-read guard never fires. A gate run by `origin/main`'s `gate-lock.ps1` can therefore have its live lock deleted, and two gates then run at once.
  - It needs an older script copy and a dead holder, and both scripts must act within the same few milliseconds.
  - The fix is one line. A scratch copy with the fix passed.
- **Non-blocking notes:** N1 to N6.

## How this round tested

- **Scratch repo only.** Every test ran in a scratch repo made with `git init` under the session scratchpad (`...\scratchpad\r3repo`). The repo held:
  - copies of `gate-lock.ps1`, `lock-common.ps1` and `build-slot.ps1` from `f9ff6d8`;
  - `origin/main`'s `gate-lock.ps1` in `old\scripts\`;
  - a stub `build.ps1`. Its first 8 lines (the gate re-entry path) match the real file, which this branch does not change (`git diff origin/main...` is empty for `scripts/build.ps1`). Its body only writes a log line with the PID, `GLANCELOT_GATE_LOCK_HELD` and the call depth.
- **Changes to the code under test:**
  - I added one guard line after `$commonDir` is resolved, in the scratch `lock-common.ps1` and the scratch old `gate-lock.ps1`: `if ($commonDir -notlike '*scratchpad*') { throw "guard: $commonDir" }`. This version's stale checks open claim files exclusively (`Test-Unheld`) and can delete them, so a test started from the wrong directory must not touch the real lock. A deliberate `-Status` call from the real checkout tripped the guard.
  - The old copy's 30-second poll was shortened to 1 second.
  - For the B1 fix check only, the scratch `gate-lock.ps1` was patched as shown under B1.
- **Real lock untouched.** No file under the real repository's git dir was created, opened, changed or deleted.
- **Load.** A real full gate was running on this PC during the review. It may be the `scratchpad\gate-c1b.ps1` process, PID 29176, started 22:49; I did not touch it. To keep the extra load low:
  - tests ran one at a time;
  - polls were 1 to 5 s and holds 0.3 to 10 s;
  - there were no busy-wait loops, and the race workers slept until a shared start time;
  - the only tight loop was a 14 s `Get-Content` reader in test 6c.

  Tests ran from 22:56 to 23:11. If that gate failed a timing-budget test in that window, treat the failure as possibly caused by this review and rerun the gate. No scratch processes were left running.

## Blocking finding

### B1. `Remove-StaleGate` deletes whatever `owner.json` holds now, not the record it judged stale

```powershell
function Remove-StaleGate {
    if (Test-Path -LiteralPath $gateOwnerFile) { [void](Remove-StaleLockFile $gateOwnerFile (Read-LockJson $gateOwnerFile)) }
```

`Remove-StaleLockFile`'s guard compares a fresh read of the file with `$judged`. Here `$judged` is itself a fresh read, so the comparison always matches. The record that `Get-GateState` actually judged stale is never used.

- **Why this case is unprotected.** A new-script holder keeps `owner.json` open without Delete sharing, so its live lock cannot be deleted. An older `gate-lock.ps1` does not do that: it writes `owner.json` with `Set-Content` and closes the file.
- **Interleaving (test 9a, run step by step in one process):**
  1. A holder died and left `owner.json` behind. The new head waiter N runs `Get-GateState` and gets `stale`.
  2. An `origin/main` waiter O2 removes the stale directory, creates it again and writes its own live record. O2 now runs its gate.
  3. N calls `Remove-StaleGate`. It deletes O2's live `owner.json` and warns `Removed stale owner.json: {"pid":42236,...,"holder":"O2"...}`. It then removes the empty directory.
  4. On its next pass, N claims `owner.json` with `CreateNew`. N and O2 now both run a full gate.
- **How likely it is.**
  - It needs three things together:
    - a stale gate lock, for example after a holder was killed or a CI run was cancelled;
    - a waiter running an older copy of `gate-lock.ps1`;
    - step 2 landing in the few milliseconds between N's two reads.
  - Older copies remain in worktrees branched before this merge. Their `build.ps1 -Test` calls their own `gate-lock.ps1`. AGENTS.md asks for those runs to go through the main checkout's script, but nothing enforces that.
  - On `origin/main`, two old waiters already share a similar window (round-1 note N5). So this is not a regression against `main`.
- **Why it blocks anyway.** It is the only overlap path this review found. The doc says an older `gate-lock.ps1` "never overlaps a gate", and the comment in `Remove-StaleGate` says "re-read guarded".
- **Fix:** pass the record that was judged stale.

```diff
-function Remove-StaleGate {
+function Remove-StaleGate($judged) {
     ...
-    if (Test-Path -LiteralPath $gateOwnerFile) { [void](Remove-StaleLockFile $gateOwnerFile (Read-LockJson $gateOwnerFile)) }
+    if (Test-Path -LiteralPath $gateOwnerFile) { [void](Remove-StaleLockFile $gateOwnerFile $judged) }
 ...
-            if ($g.state -eq 'stale') { Remove-StaleGate; Start-Sleep -Milliseconds 200; continue }
+            if ($g.state -eq 'stale') { Remove-StaleGate $g.owner; Start-Sleep -Milliseconds 200; continue }
```

- **What the fix does in each case:**
  - Dead holder: the judged record is still in the file, so it is deleted as before.
  - Empty leftover: `$g.owner` is `$null`, and `Remove-StaleLockFile` deletes the file only if it is still empty.
  - The case above: the judged record differs from the file, so nothing is deleted.
- **Fix verified on a scratch copy:**
  - 9a with the fix: step 3 left O2's lock (`state = held`), and in step 4 N was refused.
  - The compatibility tests 3a to 3e, which include both kill-and-take-over cases, passed again with the fix.
- **Other cleanup paths are correct.** `Get-LiveLocks` and the `build-slot.ps1` claim loop already pass the record they judged.

## Round-2 items

| Round-2 item | Status at `f9ff6d8` | Evidence |
|---|---|---|
| B1: a child of the gate holder running `build.ps1 -Test` recursed | Fixed. The re-entry branch sets `GLANCELOT_GATE_LOCK_HELD` to its own PID and restores the old value in `finally`. | 1a to 1c: a child, a `cmd /c` grandchild and a `pwsh -c` grandchild each ran the body once with `held=<own pid>`, in about 1 s and at the same call depth (`depth=4`) as the standalone run. 1d: a throw inside the re-entered child restored the variable. |
| S1: a reader that does not share Delete stranded `owner.json` until the holder's shell exited | Fixed. Release empties the file through its handle, retries the delete, and a claim file nobody holds open is stale (`Test-Unheld`). | 6a and 6b: a reader kept the gate file or slot file open across release for 3 s. The next gate and build started 0.5 s after the reader closed it, while the holder shell was still alive. 6c: 5 releases during a 14 s `Get-Content` loop (8864 reads) left nothing behind. |
| S2: the gate holder's nested `build-slot.ps1` could wait on slots | Fixed. Inside its own gate, `build-slot.ps1` runs at once without a slot. | 1f: `-Slots 1` in a child of the gate holder ran at once, with no slot file and no variable set. |
| S3: gate requests were not served first come, first served | Fixed. Each request takes a `CreateNew` ticket numbered one past the highest present, and only the lowest live ticket may claim. | 2a: W waited behind B's back-to-back gates for 5.4 s (one B gate plus one poll) instead of until B stopped, and B's next gate queued behind W. 2b: W1, W2 and W3 started in order. 2c: planted lower tickets (an empty unheld one and a dead PID) were skipped. Killing the head (W1) and a middle ticket (W3) gave the order W2, then W4. |
| N-a: recorded ancestor PIDs are trusted without a recycle check | Open, unchanged. | Code read. |
| N-b: a leftover `owner.json` let its holder re-enter | Fixed by the S1 fix: a released file is empty and unheld, so the owner is `$null`. | Code read; 6a. |
| N-c: an older script can lose the claim and fail | Open, documented, and never an overlap. | 3e; round-2 note. |
| N-d: Ctrl+C right after `Open-LockFile` can leak a handle | Open. With FIFO, a leaked ticket now blocks later gates as well as builds, until the shell exits. The window is still a few statements, so accept it. | Code read. |
| Round-1 N3 (lock dir follows the current directory) and N4 (CI 240-minute timeout against the 180-minute wait) | Open. N3 is documented. `windows-gate.yml` is unchanged on this branch. | Not applicable. |

## Brief checks (1 to 8)

1. **Child and grandchild re-entry:** passed (1a to 1f, above).
   - In-process `build.ps1 -Test` inside a gate (the CI pattern) ran its body with `held=<outer pid>`.
   - Standalone `pwsh -File build.ps1 -Test` took and released the gate normally.
   - Broken chain (1g): a child re-entered, started a detached grandchild with `Start-Process` and exited. The grandchild queued a ticket and ran 4.5 s after the gate ended, so it did not overlap. Its PID chain to the owner was broken, and the variable named the exited child.
   - When a plain child that inherited the variable started a detached grandchild (1h), the grandchild re-entered through the variable during the gate.
2. **FIFO and crashed tickets:** passed (2a to 2c, above).
3. **Old-script compatibility, both ways:** passed (3a to 3e).
   - An old holder blocked a new gate and a new build.
   - A new holder blocked an old waiter. The old `-Status` read the new `owner.json`, including its `claim`, `ancestors` and `ticket` fields.
   - A killed old holder was cleaned up by the new script, and a killed new holder by the old script.
   - Old waiters do not take tickets, so they can claim ahead of the queue (3e). This is documented.
   - The B1 interleaving is the one exception.
4. **Synchronized race, 40 iterations:** 0 overlaps.
   - New against new: 80 intervals, no cross-iteration overlaps, no errors, no leftovers. Request skew between the two workers was 0 ms (median) and 16 ms (maximum), and 36 of 40 starts were within 5 ms. Wins split 21 to 19.
   - New against old: 80 intervals, 0 overlaps, 0 errors. The old copy won every time, because it claims without a ticket or slot check.
5. **Two or three slot holders requesting gates:** no deadlock.
   - 5a: two holders requesting in-process ran one after the other. A plain build launched while they waited started only after both gates.
   - 5b: W, which held no slot, queued first. Three slot holders then requested gates from child `pwsh` processes.
     - W ignored the requesters' slots and ran first, and the three gates followed in ticket order.
     - The plain build started after the last gate.
6. **Stranded `owner.json` and kills:** passed (6a to 6c, 2c, 3c, 3d, and the killed holder in test 8). After a kill, `-Status` reported `stale (the next gate-lock.ps1 request removes it)`, and the next gate removed it with a warning.
7. **Throw and Ctrl+C:** passed (7a to 7f).
   - A throw in a gate `-Run` or a build-slot `-Run` left no files, and both variables were restored.
   - `PowerShell.Stop()` (the Ctrl+C mechanism) released this process's files in each case:
     - during a gate `-Run`;
     - while at the head of the queue;
     - while behind another ticket;
     - during a slot `-Run`;
     - while a slot waited on a gate.

     Other processes' files were left in place.
   - Round 2 also tested a real `CTRL_C_EVENT`; this round did not repeat it.
8. **`-Status`:** readable in each state: free, held by the old script, held by the new script with a queue, and stale. See note N1 for two small issues.

**Handle leaks (7g).** The process handle count went from 756 at the start to 707 after 30 gate cycles and 30 slot cycles in one process, then to 671 after 1000 calls each to `Test-Unheld` and `Read-LockJson`. No handle leaks per claim.

Code read:

- `Test-Unheld` and `Read-LockJson` dispose their streams in every path.
- `Open-LockFile` gives back a failed claim.
- `Close-LockFile` disposes the handle before it retries the delete.

## Non-blocking notes

- **N1. `-Status` counts stale rows as held.**
  - With one crashed ticket and one empty, unheld slot file planted, it printed `build slots: 1 held of 3; gate requests pending: 2`. In fact no slot was held and one ticket was pending. The table rows themselves show `stale`, so a reader can still tell.
  - Fix: count only `live` rows.
  - Minor: the gate line prints `started` with a 24-hour clock (`23:07:54`), while the table uses 12-hour time (`11:07:56 PM`).
- **N2. A huge ticket number stops every gate until a build cleans it up.**
  - `New-GateTicket` takes the maximum over every pending file, stale ones included, before cleaning anything up.
  - With a planted stale file `...-9223372036854775807.json`, `$max + 1` becomes a `double`, and `{1:D8}` threw `Format specifier was invalid` on every gate request. One `build-slot.ps1` run removed it, and gates worked again.
  - A 20-digit name fails `TryParse`, so it is ignored for ordering. `build-slot` still saw it until that run removed it.
  - Natural overflow is not a concern. Numbers restart at 1 whenever the queue empties (2b), so only a planted or corrupted file can reach this.
  - Fix: drop stale ticket files before taking the maximum, or treat a value of `[long]::MaxValue / 2` or more as unparseable.
- **N3. A stalled head ticket stops everything behind it.**
  - Before FIFO, a live but stalled waiter blocked only builds. Now it also blocks every later gate until it exits or reaches `-WaitMinutes`.
  - Examples of a stalled waiter: a console paused by QuickEdit selection while `Write-Host` runs, or a process suspended under a debugger.
  - `-Status` shows the blocker by PID, and the scripts never kill processes. This is acceptable, but worth one line in the doc.
- **N4. Re-entry through a broken process chain falls back to queuing.** See check 1, 1g. This is safe, because there is no overlap. It deadlocks only when the gate holder waits for that detached grandchild, and then only until `-WaitMinutes`.
- **N5. `-PollSeconds` now defaults to 5.**
  - The 180-minute wait therefore does about 2160 polls. Each poll runs `Get-ChildItem`, `Get-Process` and an exclusive-open probe on every claim file.
  - The cost is negligible, but each poll briefly opens leftover claim files exclusively. Any reader that hits that moment gets `$null` and treats the file as held, which fails safe.
- **N6. `lead-inbox.ps1` is unchanged since round 2,** so it is still read-only.

## Test output (scratch repo, trimmed)

```text
T1 re-entry inside a gate held by pid 42904:
22:56:12.956 BODY child   pid=46512 BuildDir=b-child   held=46512 depth=4   (1a exit=0, 1 s)
22:56:13.727 BODY gc-cmd  pid=8444  BuildDir=b-gc-cmd  held=8444  depth=4   (1b exit=0, 1 s)
22:56:14.679 BODY gc-pwsh pid=24220 BuildDir=b-gc-pwsh held=24220 depth=4   (1c exit=0, 1 s)
1d inside held=7204 pid=7204 / caught boom / before=42904 after=42904
22:56:15.666 BODY inproc  pid=42904 held=42904 depth=4
1f slot body held_slot=  slot files: (none), 1 s
after gate held=[] files=[]
22:56:32.230 BODY standalone pid=39880 held=39880 depth=4
1g/1h: 22:56:32.833 GATE START; 22:56:34.992 BODY detached2 (plain child, inherited var);
       22:56:40.262 GATE END; 22:56:44.821 BODY detached1 (broken chain, queued); files=[]

T2a FIFO vs back-to-back gates (W default -PollSeconds 5):
22:57:35.352 W REQUEST / 22:57:36.168 B END 2 / 22:57:40.789 W START / 22:57:41.709 B START 3
T2b 22:58:20.416 H END -> 22:58:20.626 W1 START -> 22:58:21.582 W2 START -> 22:58:22.565 W3 START; files=[]
    -Status: gate ticket 1 live 41908 W1 / gate ticket 2 live 38440 W2 / gate ticket 3 live 13480 W3
T2c planted empty #1 and dead-pid #2 skipped; queued tickets 1..4 (W1..W4);
    22:58:30.657 KILL W1 (head), KILL W3 -> 22:58:34.283 H END -> 22:58:34.442 W2 START -> 22:58:35.441 W4 START; files=[]

T3 compatibility with origin/main gate-lock.ps1 (old poll 1 s):
3a 22:59:05.404 OLD END -> 22:59:05.823 NEW START -> 22:59:08.209 SLOT START
3b 22:59:14.714 NEW END -> 22:59:15.757 OLD START; old -Status shows pid 45344, claim handle, ticket 1
3c 22:59:20.263 KILL OLD -> 22:59:21.429 NEW START; files=[]
3d 22:59:25.913 KILL NEW -> 22:59:26.643 OLD START; files=[]
3e N1 holds, N2 queued, OLD waiting: N1 END 22:59:31.943 -> N2 START 22:59:32.367 -> OLD START 22:59:33.392

T4 synchronized races (hold 300 ms, poll 1 s, 3 s ticks):
race new vs new, 40 iterations: intervals=80 overlaps=0 unterminated=0 errors=0 firstA=21 firstB=19 minGapMs=650 leftover=[]
cross-iteration overlapping pairs=0; request skew ms: max=16 median=0 under5ms=36
race new vs old, 40 iterations: intervals=80 overlaps=0 unterminated=0 errors=0 firstA=0 firstB=40 minGapMs=221 leftover=[]

T5 slot holders requesting gates:
5a 23:04:44.763 A GATE START .. 46.777 END; 47.899 B GATE START .. 49.904 END; 50.659 C (plain build, launched 45.014) START
5b 23:04:58.751 W START; 23:05:01.002 A child GATE; 23:05:05.075 B child GATE; 23:05:08.678 E child GATE;
   23:05:11.601 C START (launched 23:04:57.722); files=[]

T6 stranded files:
6a 23:05:56.075 P released (owner.json held by reader); 56.817 reader closes; 57.302 Q GATE START; 57.847 B SLOT START; P alive=True
6b 23:06:03.549 P released slot (held by reader); 04.269 reader closes; 04.747 Q START; 05.459 B START; P alive=True
6c 5 releases during a Get-Content loop (reads=8864): leftover=[] after each; Q START 0.6 s after request; P alive=True

T7 throw / Stop():
7a caught: boom; files=[] held=[] / caught: boom2; files=[] slot=[]
7b [run]               before=[glancelot-gate-lock] after=[]
7c [head waiting]      before=[glancelot-gate-lock,glancelot-gate-pending-00000001.json] after=[glancelot-gate-lock] (H's)
7d [behind a ticket]   before=[...-00000001.json,...-00000002.json] after=[glancelot-gate-lock] (W's)
7e [slot run]          before=[glancelot-build-slot-1.json] after=[]
7f [slot waiting]      before=[glancelot-gate-lock] after=[glancelot-gate-lock] (H's)
7g handles: start=756 after 30 gate+30 slot cycles=707 after 1000 Test-Unheld+Read-LockJson=671 files=[]

T8 -Status:
gate lock: free
build slots: 0 held of 3; gate requests pending: 0 (files in <R>/.git)
gate lock: held by dvort on OLDH (pid 49740) since 10/22/2026 23:07:47
gate lock: held by dvort on NEWH (pid 50968) since 10/22/2026 23:07:54
build slots: 1 held of 3; gate requests pending: 2 (files in <R>/.git)
kind          state    pid branch  started                cwd
gate ticket 1 live   33528 W1      10/22/2026 11:07:56 PM <R>
gate ticket 9 stale 999999 crashed 10/22/2026 11:06:57 PM
slot 2        stale
gate lock: stale (the next gate-lock.ps1 request removes it)
WARNING: Removed stale owner.json: {..."pid":5184,...,"branch":"KILLED",...}
gate lock: free

T9 Remove-StaleGate interleaving (B1), f9ff6d8 code:
9a step 1  N: Open-LockFile failed, Get-GateState = stale (owner pid 999999)
9a step 2  O2 now holds the gate: state = held, owner = old-gate pid 42236
           warning: Removed stale owner.json: {"pid":42236,...,"holder":"O2","branch":"old-gate"}
9a step 3  N: Remove-StaleGate -> files=[] state = free
9a step 4  N: Open-LockFile owner.json -> CLAIMED: N runs its gate while O2 (pid 42236, alive=True) also runs
with the fix:
9a step 3  N: Remove-StaleGate -> files=[glancelot-gate-lock] state = held
9a step 4  N: Open-LockFile owner.json -> refused

N2 ticket overflow (planted stale ...-9223372036854775807.json):
ERROR: Error formatting a string: Format specifier was invalid.   (3 gate requests in a row)
build-slot: WARNING: Removed stale glancelot-gate-pending-9223372036854775807.json ... SLOT ok; files final=[]
```
