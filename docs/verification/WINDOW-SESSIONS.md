# Window slots and resume verification

Scope: WINDOW-SLOTS-01 and WINDOW-RESUME-01 (UX-08, UX-12, SYS-03).
Reviewed Claude's commits `7b30921`, `434b9ab`, `53d1539` against `619b511` on October 17, 2026.

## Claude's changes

- Opening-order window slots coordinated across independent processes, with process identity checks, OS-released file locks, stale-process pruning, and reset after the last window exits.
- Shared atomic settings updates that merge only changed preferences, preserve unopened slots/unknown keys, guard future schemas, keep a last-good backup, and support explicit reset.
- Debounced successful-image location saves, per-slot resume, explicit-file precedence, and missing/deleted/empty/inaccessible-source recovery.
- A settings explanation and regression coverage for slots, concurrent writers, interruption, recovery, and resume.

## Review corrections

Independent review found and verified these fixes:

1. Keep slot ownership until the controller finishes its final save. Releasing on aboutToQuit previously allowed a destructor retry to overwrite a newly claimed slot.
2. After a delayed claim, invoke restore and retain the last successfully decoded image even if decoding happened before the claim. A later failed open cannot become the remembered image.
3. Validate shortcut changes against the merged map while holding the settings lock. Concurrent conflicting edits are rejected instead of invalidating all shortcuts on restart.
4. Interactive writes/slot claims make one nonwaiting attempt; position saves retry on a bounded timer. Final teardown retains bounded blocking retries. Lock-free readers retain short retries for Windows atomic replacement transients.
5. Refuse counter exhaustion while any slot remains live rather than recycling slot 1.

The settings binding message now reports the controller's actual saved/session-only/conflict status. Existing palette and layout are preserved.

## Verification

The lead used an isolated `agent/session-review` worktree/build. Independent reviewer approved the final code, process harness, and native compact screenshots; no open review findings.

Worktree command:

```powershell
./scripts/build.ps1 -BuildDir build-session-review -QtRoot C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64 -Test
```

Actual output:

```text
1/7 Test #1: navigation ....................... Passed  1.47 sec
2/7 Test #2: viewer_acceptance ................ Passed  4.04 sec
3/7 Test #3: thumbnails ....................... Passed  5.32 sec
4/7 Test #4: folder_tree ...................... Passed  4.75 sec
5/7 Test #5: window_slots ..................... Passed 17.70 sec
6/7 Test #6: viewport ......................... Passed  0.06 sec
7/7 Test #7: themes ........................... Passed  0.17 sec
100% tests passed out of 7
Total Test time (real) = 33.68 sec
```

Reproducible process check (matching Qt bin on PATH for an undeployed build):

```powershell
.tools/python/Scripts/python.exe scripts/verify-window-resume.py build/glancelot.exe
```

Actual worktree output:

```text
PASS: two independent processes retain positions across full restart and reverse close order
PASS: explicit file overrides resume and unopened slot is retained
PASS: real retry timer restores a saved image and remembers an explicit image decoded before claim
PASS: final flush holds slot ownership while a new process opens
PASS: all generated source media remain byte-identical
```

The process harness uses temporary generated PNGs and isolated settings. It holds actual OS locks, starts real viewers, checks slot/image/decode reports, and verifies every source byte after viewing. Unit tests additionally cover simultaneous launches, crashes, atomic writer interruption, future/corrupt schemas, backup/reset, unknown preferences, unopened slots, changed sort position, deleted images, missing/empty/junction folders, failed decode, explicit-open cancellation, and GUI contention recovery.

Native Windows 540x360 settings screenshots were inspected in dark/light themes and with memory enabled/disabled. Text wraps without clipping; stderr is empty. Offscreen screenshots initially lacked fonts, so they were not used as visual evidence. Local artifacts live under `.worktrees/session-review/.cache/session-review/`.

## Integration gate

Runtime fixes integrated on main at `85336a4`. `scripts/build.ps1 -Test -Package` passed:

```text
navigation 1.44s; viewer_acceptance 4.16s; thumbnails 5.51s;
folder_tree 4.84s; window_slots 17.12s; viewport 0.04s; themes 0.10s
100% tests passed out of 7; total 33.50s
```

The portable package opened and resumed a generated 13x9 PNG with Qt development paths removed, native Windows rendering, exit 0, and empty stderr. Package SHA256: `B16F349E5CD5F840E7BEB323EB85E1D6F2A7D69F50FC01E150423701D261E56D`.

An integrated process run exposed a fixture interaction: report files were written beside watched media, so another viewer's report could trigger hot reload near timed exit. The harness now isolates media from reports/traces and observes startup progress before releasing the claim lock. All five checks passed against the integrated binary with that isolation. The independently reviewed harness follow-up `b38051f` passed all five checks again after integration; `integrated-process-final.log` records the final output. WINDOW-SLOTS-01 and WINDOW-RESUME-01 are complete for the declared Windows scope. Logs: `.cache/session-review/integrated-build.log`, `integrated-process.log`, and `portable-smoke.log`.

## Limits

- Windows 11 / Qt 6.8.3 / MSVC 2022 verified here. Linux GUI/process locking, physical mixed-DPI/HDR, real disconnected SMB shares, and a clean-machine install are unverified.
- Position writes retry eight times during browsing and make a bounded final attempt on exit. Persistent filesystem failure can prevent saving; existing saved positions remain intact.
- Preference edits blocked by another writer apply to the current session and report that persistence failed. They do not silently overwrite other settings.
- This completes the numbered-slot/resume slice of M2.1, not single-instance IPC reuse, automatic reopening of all windows, geometry/zoom restoration, or the remaining M3 roadmap.
