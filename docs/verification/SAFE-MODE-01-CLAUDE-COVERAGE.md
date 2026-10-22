# SAFE-MODE-01 review, part 2 (same pinned checkout 0580fe0)

**No additional blocking defect found.** I found three low-severity items and one evidence gap. The original findings 1–7 from the first review stand unchanged. I have not reviewed Codex's fixes. I read files only and executed nothing.

## What I audited this round
- **Tests and wiring read in full:**
  - `tests/recovery_startup_tests.cpp`
  - `scripts/verify-recovery.py`
  - `cmake/RecoverySources.cmake` and `cmake/RecoveryTests.cmake`
  - the root `CMakeLists.txt` hunks in the patch
  - `src/app/settings_package.hpp`
  - the package read and write code in `settings_recovery.cpp`
- **Registry extraction, checked from source:** I compared the removed block in the patch (`safe-source.patch:2581-2655`) with `src/app/command_registry.cpp:10-84`.
  - They match in order, ids, labels, default bindings, `hold`/`repeat` flags, the Windows/other Delete branches, and the five presentation commands.
  - `resolveCommandBindings` (`command_registry.cpp:88-110`) is the removed `load()` logic. The default merge, user-choice-over-new-default rule, overlap handling and error text are the same, as are the `usableBindings` helpers in `binding_validation.hpp`.
  - The initialization-order fix (a function-local static) is sound. `viewer_controller.cpp:79` copies a span over that static.
- **Layout compatibility, checked from source:**
  - `layout_defaults.hpp` is the moved `PaneLayout::defaults` body, unchanged.
  - The policy ranges match `PaneLayout::validPane` and `load` (`pane_layout.cpp:20-27, 36-65`): extent 32–4096, order integer 0–99, ratio 0.1–0.9, the region and mode sets, and exactly version 1.
  - The policy is stricter than the loader. It rejects the whole plan on any invalid selected field, where the loader silently skips it. That matches the frozen contract.
- **Package implementation, complete:** I found no defect.
  - Fixed names and a manifest sealed last.
  - A 4-entry cap, 8 KiB manifest and 1 MiB members.
  - Lowercase hex only, and a stale `.manifest-seal` makes the package inapplicable.
  - An absent member with a file present is refused.
  - sp1 is computed over the exact manifest bytes.
  - A manifest-too-large failure after the members are written leaves an inert partial package.
- **Build wiring:**
  - Production sources sit outside `BUILD_TESTING`, and attachment is idempotent.
  - The hook macro is defined only on `recovery_tests` and `recovery_controller_tests`.
  - There is no `EXISTS` guard or skip, and `find_package(Python3 REQUIRED)` and Xvfb `REQUIRED` fail the configure.
  - The recovery test sources contain no `QSKIP`.
  - Resource prefix is `/` with base `assets`, matching production.
- **Evidence totals:**
  - Linux: recovery 135/135, startup 59/59, controller 10/10, fs_watch 83/83 and its budget test 3/3, all with zero skips.
  - Windows quiet run: 135, 59 and 10 with zero skips, fs_watch 82/82, budget 3/3, and the full run 37/37 in 578.32 s (`candidate-windows-quiet.txt:130-132`).
  - Process results: `result.json` shows `passed` for `run-azgvnfyf` (Linux), `corrected-full-run-1k6x8tks`, `newline-green` and the top-level Windows run.
  - The `newline-red` and `zero-frame-failure` results are retained failures.
- **RHI and RGB proof:**
  - Linux: backend 2, Mesa llvmpipe, 18 swaps, first image frame 817 ms, window exposed, zero scenegraph invalidations.
  - Windows corrected run: backend 3, CPU device 5, 35 swaps, first image frame 1150 ms.
  - Both platforms sampled exactly (255,0,0), (0,255,0), (0,0,255) at x=570/590/610, y=390. The checker requires at least 200 on the expected channel and at most 80 on the others, so the proof is tolerance-bounded, not bit-exact.
  - Samples come from the grabbed window at the canvas centre, so they are real render output, not request flags.
- **Failure paths:** the process script covers conflict, argument errors, future primary, tamper, last-good selection, live-owner refusal, kill after a dirty checkpoint, lazy restart, offline restore, and a failed explicit open. Unicode package and image names, `--`, a dash-leading image name, and a deliberately invalid QPA platform for headless runs are also covered.

## Additional findings

### L1 (Low) — restore with a layout missing from the source leaves a version-less husk
- **Where:** `src/app/settings_policy.cpp:31-37` (`setAt`) and `:127-135`.
- **Trigger:** restore layout scope when the source has no `layouts.<slot>` but the current one does. The owned leaf keys are removed, including `version`, while non-empty parents and unknown keys stay.
- **Effect:** `PaneLayout::load` then treats the entry as invalid (`pane_layout.cpp:48`) and falls back to defaults. The next `PaneLayout::persist` replaces it with `{}` (`pane_layout.cpp:74`), dropping the unknown nested data. That is arguably tolerable, but it conflicts with "preserve unknown panes/nested values" once the window is used.
- **Fix:** when the source layout is absent, remove the whole slot entry if no unknown keys remain. Otherwise keep a valid `version: 1`.
- **Test:** restore a layout absent from the source, then run a `PaneLayout` persist and compare the unknown keys.

### L2 (Low) — report has no timestamp, so the quit-after margin is unmeasurable
- **Where:** `app/main.cpp:246-305` (`--quit-after` 2500 in `verify-recovery.py:159`), and the report fields.
- **Evidence:** `first_image_frame_ms` is measured from before QML load, while the quit timer starts after load. The quiet Windows run's first image frame was at 2890 ms, which exceeds the 2500 ms timer. So the margin between the first swap and report time depends on engine load time and is not recorded.
- **Impact:** I cannot rule out that load or CPU contention can leave the report running before the first swap, giving `frames = 0`. This is a hypothesis only.
- **Fix:** add diagnostic fields to the report (report time since `elapsed`, and time at timer creation). Do not change the assertions or the budget.

### L3 (Low, coverage) — process-level gaps
- Layout scope is never run through the executable.
- No process test uses a non-ASCII configuration directory for `GLANCELOT_CONFIG_DIR`. The Unicode private-root case is covered by the startup helper subprocess, which does call `_wputenv_s` with a Unicode parent.
- `digest_tree` (`verify-recovery.py:31-33`) hashes files only, so a stray empty directory in the original namespace would not be detected.
- Cancellation is covered only by in-process tests, not by a signalled executable.
- `recovery_startup_tests.cpp` has no case for `-qmljsdebugger` (my original finding 6).

### E1 (evidence gap) — quiet Windows raw artifacts are not in this checkout
- `process-windows/quiet-full-run-lbm1hvj2` does not exist here. Only `.cache/qualification-update` logs are present: `candidate-windows-quiet.txt`, `windows-quiet-*-results.txt` and `SAFE-MODE-01.md`.
- I confirmed the 37/37 and the per-target totals, but I could not independently inspect that run's reported 22 swaps, 2890 ms first frame and RGB samples. Those figures rest on the narrative.

## The zero-swap attempt remains unexplained
- **Evidence:** the retained `zero-frame-failure/viewer-024.json` has a canvas of 2360×1560 (exactly 2× the 1180×780 default) at DPR 1. The decode succeeded at 1452 ms, `frames = 0`, and `rhi.observed = false`.
- **Not seen in good runs:** every good run has 1180×780. In the corrected run (`corrected-full-run-1k6x8tks`, the earliest artifact with a `window` block) the screen was 2240×1463, smaller than the failing run's window.
- **Meaning:** the retained report predates the `window` and `render_callbacks` fields, so I cannot tell whether the window was resized, recreated or moved to another screen. Treat this as a lead for the lead, not an explanation.

## Still uninspected
- The raw quiet Windows process directory (E1).
- Per-command stdout and stderr contents beyond result totals, and the screenshots as images.
- `app/settings_maintenance.hpp`, `startup_intent.hpp`, `startup_environment.hpp` and `recovery_session.hpp`. The relevant `.cpp` behaviour was read.
- All of `tests/recovery_tests.cpp` and `tests/recovery_controller_tests.cpp` beyond test names and the sampled bodies.
- Qt 6.8.3 internals (`QSaveFile::commit` sync behaviour, `-qmljsdebugger` handling).
- Codex's follow-up fixes, and any integrated or merged state.
- Physical GPU, DPI and power-loss behaviour, which remain unverified.

## Status
I have audited the remaining coverage listed in your request, apart from the items above. I still found no blocking defect against the frozen engine contract. The recommendation stays conditional, as in the first review. Findings 1–3 from the first review should be resolved or explicitly accepted. The Codex fixes need a separate pinned follow-up, and integrated gates must still pass. I am not claiming integration or done.