# PLATFORM-01 integration review (Claude) - merged main 73fa157

Candidate: `73fa157ea3be71a27bbfd2abe79021617eca14ee`. Reviewer lane: Claude, tree `.worktrees/claude-review-platform-integration`, build `build-claude-review-platform-integration`.

## Verdict: CHANGES REQUIRED - NOT APPROVED. The native Windows integration gate remains FAILED and the diagnosis is incomplete.

I did not run the authoritative `scripts/build.ps1 -Test` gate myself. The root had paused heavy native suites, and a session interruption ended my probe work before I reached it. The only native runs here are single-case `directory_model_tests.exe` runs. I found no product defect and no supported fix. I propose no threshold, skip or measurement change.

## What the evidence supports

Probe setup: `largeFolderStaysResponsive` was run alone, with temporary instrumentation in my own tree. The instrumentation has since been reverted.
- A pure spin thread reading `steady_clock`.
- A native thread doing `sleep_for(5ms)`.
- A thread posting a queued event every 5 ms to the GUI thread and measuring delivery latency.
- External per-thread wait-state sampling.
- Timing around the GUI-side publish/apply step.
- An in-process stack snapshot of all threads when the GUI heartbeat was stale.

Each launch mode (`normal` and `CREATE_NO_WINDOW`) was run from a Python harness using the project Python.

1. **The oracle measures real event-loop unavailability, not timer coalescing.**
   - Queued-event latency matched the Qt timer gap to within about 5 ms in every run (for example 1263 ms against 1268 ms, 3181 ms against 3181 ms, 63.5 ms against 69 ms).
   - The native 5 ms sleep thread never exceeded 9 ms.
   - The spin thread never exceeded about 4 ms.
   - So the process was not descheduled and timers were not coalesced.
   - This is consistent with the author's finding that a process-local timer-resolution request did not help, and it makes that avenue unsupported.
2. **`CREATE_NO_WINDOW` is not shown to be the cause.**
   - Normal launches also failed in my runs.
   - Over the runs, 5 of 6 sampled `normal` runs failed, against 5 of 6 `nowindow` runs, and the failures varied from 69 ms up to 12.6 s.
   - The author's single paired run is within this variance.
3. **The model's GUI-thread code is not the stall.** Publish plus apply took 0.6–28 ms. The worst gap was roughly the whole listing duration (for example 4040 ms gap against a 4508 ms listing; 69 ms gap against a 193 ms listing). The gap and the listing wall time scale together.
4. **During stalls both the GUI thread and a Qt pool thread were inside win32k (USER) syscalls.** Stack snapshots at stall time showed:
   - the GUI thread in `NtUserPeekMessage`, `NtUserKillTimer`, `NtUserSetTimer` or `NtUserMsgWaitForMultipleObjectsEx`, called from `QTest::qWait` / `QEventDispatcherWin32::processEvents`;
   - a pool thread in `NtUserCreateWindowEx` (creating its event-dispatcher window);
   - the probe's post thread in `NtUserPostMessage`;
   - the QtConcurrent worker idle in `QWaitCondition::wait` in at least one capture.
   The threads that make no USER calls (spin and `Sleep`) stayed on time. This suggests slow or serialized USER-subsystem calls for this process, but it is a hypothesis and not a proven cause.
5. **Weak independent corroboration.** An independent Python process timing `PeekMessage`/`SetTimer`/`KillTimer` for 90 s with no test of mine running recorded 5 stalls of 37–75 ms (`.cache/usermon-idle.txt`). That shows ambient USER-call latency on this session, but it is far smaller than the 1–12 s stalls and is not shown to coincide with them.
6. **Confounders.** Other lanes were active throughout (`ctest`/`grid_ui_tests` in `claude-f040`, `directory_model_tests`/`ninja` from other sessions, and a `ui_settings_tests.exe` idle since 08:54). Runs were not isolated, so I cannot separate environment load from a product effect. The author's quiet-machine full run passing with 44/40/46 ms gaps fits load-sensitivity, but I did not reproduce a quiet-machine pass.

## What I cannot conclude

- Whether a product, harness or OS component causes the USER-call stalls. Not proven.
- Whether the stall tracks a specific concurrent process (a build or another GUI test). Not tested.
- Why the stall scales with the worker's listing time.
- Whether the shipping viewer is affected. The stalls are in a `QCoreApplication` test using `QTest::qWait` polling, not in the viewer's render loop.

## Proposal

No fix is proposed. A process-local harness change is not supported by the controlled evidence. Do not raise the 250 ms limits, skip the cases, change priority or alter power settings.

## Minimal next probe (for the lead or a later reviewer)

1. With all other lanes' native work paused and the machine quiet, run the single case 10 times in each launch mode. Record pass/fail and gaps. This gives the quiet-machine baseline I did not obtain.
2. Run the independent `PeekMessage`/`SetTimer` monitor (`.cache/usermon.py`, kept untracked in my tree) concurrently with each run, and compare its stall timestamps with the test's stall window. If the monitor sees multi-hundred-millisecond stalls at the same time, the cause is outside Glancelot code.
3. Only then run the authoritative `scripts/build.ps1 -BuildDir <dir> -Test` gate.

## Hygiene

- `src/app/directory_entry_model.cpp` and `tests/directory_model_tests.cpp` were temporarily instrumented in this tree and restored with `git checkout`. This commit contains only this report.
- **Disclosure:** early on I mistakenly edited `C:\src\Github\Glancelot\src\app\directory_entry_model.cpp` in the root checkout, because a search path pointed there. I reverted it exactly within minutes. `git diff` for that file in the root was empty afterward, and root `git status` shows no change to it. Root status still shows `docs/UI-SETTINGS-01-PLAN.md` and `tests/ui_settings_tests.cpp` modified; those are not mine.
- No machine-wide setting, service or other process was changed. No merge or push. The root integration lock was not touched. `.cache/native-suite-running` was never created because I did not run the full suite.
- Earlier merged-source evidence (Windows package wrapper 18/18, Docker 18/18, Linux GUI package 18/18) is the author's, not re-verified here.
