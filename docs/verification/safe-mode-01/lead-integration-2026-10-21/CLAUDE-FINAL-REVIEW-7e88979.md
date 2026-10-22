# SAFE-MODE-01 final independent source review (Claude Sonnet 5.5, cross-lane)

**Disposition: ACCEPT for integration at `7e88979`, on the scope below. No required changes.** I found no new blocking defect against the frozen contract. I also found no regression to inherited owners in the areas I read. The prior reviews' limits still stand.

I used only Read, Glob and Grep, so nothing was executed. I did not start or check any gate, and the pending integrated Windows and Docker runs are not claimed here. I wrote no plan file, as you asked.

## Ranked findings (all nonblocking)

**1. Low (process note, required of the integrator): a failed `docker build` may not fail the `-Run` block.**
- Where: `scripts/gate-lock.ps1:38` (`try { & $Run }`) and `scripts/build.ps1:40`.
- `build.ps1` throws on a nonzero native exit, so its failure propagates through the wrapper.
- A bare `docker build` inside `-Run { ... }` does not throw. `$ErrorActionPreference='Stop'` does not cover native exit codes.
- The wrapper can therefore finish normally after a failed Docker build.
- When wrapping Docker, check `$LASTEXITCODE` inside the block (for example `docker build ...; if($LASTEXITCODE){throw}`) and record the actual exit code.
- The lock release in `finally` and the re-entrancy check (`GLANCELOT_GATE_LOCK_HELD -eq $PID`) read correctly.

**2. Low (unchanged, sync carryover): conditional `ExportTests` include.**
- Where: `CMakeLists.txt:17-19`.
- It is guarded by `EXISTS src/core/selection_export.cpp`. My Glob found only `include/glancelot/selection_export.hpp`, with no `.cpp` and no `tests/selection_export_tests.cpp`.
- The guard is therefore inert in this tree and the include is never taken.
- This is not SAFE wiring. The plan's "no EXISTS guard" rule is about recovery, and the recovery wiring is unconditional (see accepted areas).
- When EXPORT lands, the integrator should make the include unconditional, as `cmake/ExportTests.cmake:2` says.

**3. Low (test hygiene): stale executable name.**
- Where: `tests/recovery_startup_tests.cpp:124`.
- The check accepts `recovery_registry_component.exe` as an alternative. That is a dead name, so the check is slightly looser than it needs to be.

**4. Low (behavior, unverifiable offline): inline Qt option syntax is now refused.**
- Where: `app/startup_intent.cpp:68-69`, locked in by `recovery_startup_tests.cpp:103`.
- `--platform=x` and `-title=x` now exit 2, and `-qmljsdebugger` is accepted only in its inline form.
- If Qt 6.8.3 accepted these spellings, this is a small change in what the viewer accepts.
- The earlier "`-qmljsdebugger` omitted" finding is fixed (it is in `qtValues` at `:19`).
- I could not check Qt's exact option grammar offline.

**5. Low (diagnosability): failed safe-mode preparation leaves a stale environment variable.**
- Where: `app/startup_environment.cpp:42-61`.
- If a later `qunsetenv` or `qputenv` fails, or the platform is unsupported, `GLANCELOT_CONFIG_DIR` still points at a temp root that the destroyed owner has deleted.
- `main.cpp:95-99` returns immediately in that case, so no consumer sees it.
- The unit test covers an invalid parent (`WriteFailed`) at the helper level. No process-level test covers `startupError` from `main`.

**6. Low (oracle precision): the process-test step count is not an assertion.**
- Where: `scripts/verify-recovery.py:347`.
- It prints `len(steps)` but compares it to nothing.
- I counted 35 `run()` calls in order: 10 settings-diff, backup and restore steps, then 8 invalid arguments, 2 help/version, 7 other backup/diff cases, 3 safe/normal GUI runs, and 7 in the restart phase.
- That is consistent with 33 to 35, with the two added commands being the layout preview and apply at `:227-235`.
- The proof is in the per-step `require`s, which are real. Layout defaults must drop only owned keys (`:234-235`). Other slots and unknown panes must survive byte-for-byte.
- The count alone proves nothing, and a skipped step would raise an exception rather than lower the count.

**7. Low (timing sensitivity): the restart phase waits on wall-clock deadlines.**
- Where: `scripts/verify-recovery.py:285-298`.
- It waits up to 12 s for the viewer's first dirty checkpoint. Each subprocess has a 25 s timeout.
- That fits the 180 s CTest limit. It could flake on a machine loaded by something other than the serialized gates.
- It fails closed.

## Areas closed from the earlier "not inspected" list

- **`recovery_startup_tests`:**
  - The target is `QCoreApplication`-only, and the test asserts no `QGuiApplication` exists.
  - The registry is captured before `main` from another translation unit.
  - Coverage is meaningful:
    - 40 or more rejected command shapes (orphans, repeats, ambiguity, Qt inline options, every maintenance and viewer-option combination).
    - Unicode and `--` handling.
    - Scope, slot and fingerprint selection.
    - A subprocess safe-environment helper that proves ordering before application construction.
    - Original files untouched, the private root held until the context is released, and the default-descriptor and invalid-parent cases.
  - It does not run `maintenance()` itself. The process test covers that.
- **`verify-recovery.py` oracle:**
  - Headless runs use an invalid QPA platform, so any accidental `QGuiApplication` fails.
  - They also assert the controller trace file is absent. `viewer_controller.cpp:248` writes `controller:construct` on construction, so the oracle can detect a controller.
  - Stdout must end in a bare `\n` and be at most 3 MiB.
  - The checks that are real:
    - Preimage bytes equal the raw original.
    - A stale or zero fingerprint gives exit 3.
    - A backup-only edit invalidates the plan.
    - Future primary gives exit 5. Tampered package gives exit 6.
    - A live owner gives `Busy` (exit 7).
    - The viewer is killed after an observed dirty checkpoint, which stays dirty. The next launch resumes lazily with a new nonce.
    - Offline restore leaves an empty nonce with origin `restore`.
    - A failed explicit open does not overwrite the prior position.
    - Safe-mode directory digests are identical before and after.
    - Safe mode renders actual RHI frames and checks screenshot RGB samples.
  - `QSG_*` and software overrides are scrubbed from the environment, and the unsafe renderer variables are injected only for the safe-mode runs.
  - A process kill is not a power-loss test, as `verify-recovery.py` itself says.
- **Shared CMake and production-hook separation:**
  - `GLANCELOT_RECOVERY_TESTING` is defined only on `recovery_tests` and `recovery_controller_tests` (`cmake/RecoveryTests.cmake:11,40`).
  - It appears only in private `src/app` files, with no public header gated.
  - The `#ifdef` blocks are in `settings_transaction.hpp`, `settings_package.hpp`, `settings_file.cpp`, `settings_recovery.cpp` and `recovery_session.cpp` (a private struct inside the `.cpp`).
  - `glancelot_recovery_common`, `recovery_startup_tests`, the viewer and the consumer targets compile without it.
  - Production wiring is unconditional: `CMakeLists.txt:84,259`.
  - Python is `REQUIRED` (`RecoveryTests.cmake:55`), and Xvfb is required on Linux.
  - The attach function skips already-present sources and is idempotent. The two hooked targets compile their own engine and do not link the common library.
- **Settings package validation** (`settings_recovery.cpp:205-249`):
  - Fixed names only. A fourth root entry is rejected, with no recursion.
  - Manifest limit, schema 1, `sealed` flag, exactly two ordered descriptors.
  - Absent members need length 0 and the zero hash, and the file must not exist on disk.
  - Lowercase SHA256 is checked, plus size and aggregate bounds.
  - A policy revision `policy1:` is hashed from the shipped data.
  - I did not re-derive the per-field policy tables in `settings_policy.cpp` line by line. I relied on the prior reviews and the process-test assertions for those.
- **Maintenance adapter** (`app/settings_maintenance.cpp`, `app/main.cpp:86-93`):
  - Exit codes match the plan.
  - Output is bounded below 3 MiB with an explicit reserve.
  - The restore path re-previews and compares the fingerprint before any mutation.
  - Argument errors, help and version run before `QGuiApplication` is built.
- **Session engine** (`recovery_session.cpp`, `recovery_checkpoint.cpp`):
  - A failed begin retries on the next accept and offers a read-only resume fallback (`recovery_session.cpp:107-116`). That closes my original finding 2.
  - Closing retries now wait 50 ms between attempts (`:95-99`). That closes my original finding 4.
  - The held-state structure is a shared `State`, so a detached worker cannot outlive its data.
  - The claim and sequence checks match the plan.
  - A checkpoint conflict leaves the window dirty (`:125`).

## Prior findings and residuals (not discarded)

- The original High finding (`Busy` on preference saves), the begin-failure resume problem and the live-Session restore desynchronisation were all corrected, as the successive follow-ups recorded. I did not re-verify each fix in this pass.
- The lead-accepted residuals stay as recorded. They are the binding refresh and fallback edge cases (R1, R2), feedback ownership by string token (N2/F3), and a suppressed terminal sort failure (F1 in `CLAUDE-REVIEW-03bd326.md`).
- Also accepted: the teardown 4 s budget and the 128-channel cap (N3), and the sort-failure test gap (F2).
- Sanitizer, power-loss durability (QFile flush without fsync before the manifest rename) and physical GPU/DPI remain unverified.
- The `fs_watch_budget` 180 s timeout risk is still unresolved and not waived. My read of the sync diff shows no limit or assertion change, and neither the watcher fixture review nor this one explains the earlier timeout.

## Acceptance scope and inspection limits

- **Accepted:** the source and wiring areas I read above, against the frozen contract, as nonblocking-clean.
- **Not covered:**
  - Gate results.
  - The command outputs, since I ran nothing.
  - Qt and OS behaviour, which is source-read only.
  - Full line-by-line policy, diff and restore re-derivation.
  - The Dockerfile and Linux Xvfb path.
  - Raw artifacts and media, which I excluded by instruction.
  - `window_slot.cpp` and most of `viewer_controller.cpp` this round. For those I relied on the three earlier follow-ups (`CLAUDE-FOLLOWUP-7bffd68`, `CLAUDE-REVIEW-1a205fc` and `CLAUDE-REVIEW-61e4752`).
- **Parent's checks:** the integrated Windows and Docker gates, the 35 process commands running to completion, and the `fs_watch_budget` timing outcome.
