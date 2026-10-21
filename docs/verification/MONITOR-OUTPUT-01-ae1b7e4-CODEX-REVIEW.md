# MONITOR-OUTPUT-01 pinned fix review (Codex lane)

Candidate: `ae1b7e42edf89a15b637b6c7364ba6f6fa8e92fe` (`claude/monitor-output-fix`). Compared `ae1b7e4^..ae1b7e4` with `docs/verification/MONITOR-OUTPUT-01-CLAUDE-REVIEW.md`, the packet plan and acceptance criteria. Reviewer checkout: `.worktrees/codex-review-monitor-output-fix`, branch `codex/review-monitor-output-fix`, build `build-codex-review-monitor-output-fix`. Rules read at `10bdeb1:AGENTS.md`.

**Disposition: CHANGES REQUIRED.** All 24 original tests pass, but three independent regression probes fail. This is not integrated-commit qualification.

## Findings

1. **P1 — Hidden secondary pixels survive budget eviction and low-memory release, outside the reported aggregate.** `src/app/display_manager.cpp:147-185,510-513`; `src/render/presentation_window.cpp:31-35`. `retainedBytes` counts only manager fields. Mirror/Pinned image application never clears the window's secondary canvas, and hiding that canvas does not clear its QImage. Reproduction: display A with comparison B, switch to Mirror, lower budget to one image, update to C, and call releaseCachedImages. The manager reports 6400 bytes while the hidden secondary still retains another distinct 6400-byte image. The probe fails after the supposed release, proving CPU ownership exceeds accounting. GPU retention was not measured. Clear unused window images and account for all owners before claiming aggregate ownership/low-memory release; test the comparison-to-mirror transition under pressure.

2. **P1 — Output-specific viewport/color/scale remains incomplete.** The new per-id fit command is useful, but it does not deliver the prior required per-output zoom/pan/color/scale or linked/independent comparison viewports. The author explicitly marks this PARTIAL in `MONITOR-OUTPUT-01-FIX.md`; no lead-approved acceptance reduction is present in the reviewed packet. Finish the frozen packet criteria or obtain an explicit lead scope decision before approval. Unknown color-space metadata is more honest than a fabricated sRGB report, but is not output color handling.

3. **P2 — Clearing comparison content erases stale-result history.** `src/app/display_manager.cpp:482-495,510-513` (also budget eviction at 161). compareWithPinnedOrPrevious/releaseCachedImages resets comparisonGeneration to zero. Reproduction: accept explicit comparison gen20, execute the new compare command (now showing the previous 64x64 image), then deliver old comparison gen7. It replaces that selection with the stale 30x30 image. Preserve a high-water mark separately from retained content, and invalidate requests on explicit selection changes. This is an exposed API lifecycle defect; no current production asynchronous comparison producer was identified, so broader real-world incidence is not claimed.

4. **P2 — Invalid fit input overwrites remembered valid output state.** `src/app/display_manager.cpp:498-501`; `src/render/presentation_window.cpp:53-57`. The window validates mode but the manager stores it first. Reproduction: set fill, set bogus (live window correctly stays fill), close, reopen. The new window is shrink because the stored bogus mode is rejected. Validate before mutating fitModes and test reopening after invalid input. The existing test checks only the live window, missing persistence corruption.

5. **P2 — Rebindable dismissal still excludes accepted chord bindings.** `src/render/presentation_window.cpp:88-94`; `src/app/viewer_controller.cpp:51-59,1426-1450`. setBinding accepts multi-step QKeySequence values and checks their prefixes, but the presentation key handler only matches bound.count()==1 and has no chord state or registered Shortcut. An accepted presentation-toggle chord therefore cannot dismiss the output from its focused window; Escape remains available. Route accepted bindings through a chord-aware resolver or constrain them consistently at assignment, with real-event precedence/cancellation tests. This finding is source verified, not a new runtime chord experiment.

## Prior findings assessment

- Identity: Windows device-interface product code and topology ordinals improve identity/uniqueness. Serial-bearing reorder/reconnect is covered by pure-input tests. Identical serial-less panels can still exchange identities after reorder; author disclosure is explicit. Native EDID serial, ICC/HDR and real hardware stability remain unverified; no hardware capability approval.
- Comparison command: previous/pinned secondary routing now exists and passes its original test; synchronized viewport handling is still absent and clearing-generation semantics fail above.
- Memory: two canvases now split a finite 256 MiB texture admission cap; this is a useful bound, but the hidden QImage ownership defect invalidates the aggregate CPU claim. A low-memory method exists without an automatic platform caller, as disclosed.
- Hotplug/failure/lifecycle: original offscreen signal injection, failed-decode and 50 reopen/close tests pass. These simulate handler paths; they do not physically change OS topology. Remembered-output rebinding, suspend and GPU loss remain absent/unverified.
- Escape and single-key rebinding: original test passes, with exactly one close notification. Chord support remains as above.
- No image decode or blocking directory scan was introduced in UI/render code.

## Independent commands and actual output

Build only the monitor target using the candidate `scripts/build.ps1` initialization/configuration, with its one build line changed in an ignored reviewer runner to `cmake --build $BuildDir --target monitor_output_tests --parallel 4`. The runner is `.cache/review-build.ps1`; no source CMake/configuration was edited. Command: `& ./.cache/review-build.ps1 -BuildDir build-codex-review-monitor-output-fix`. Actual output: `monitor-output-fix-codex/windows-build.txt` (34/34, exit 0).

Qt bin was placed on PATH and GLANCELOT_CONFIG_DIR pointed to a disposable reviewer `.cache/runtime-config`. Original suite command: `monitor_output_tests.exe -o docs/verification/monitor-output-fix-codex/windows-original-cases.txt,txt`.
```
Totals: 24 passed, 0 failed, 0 skipped, 0 blacklisted, 123ms
```

Test-only probes were temporarily inserted into the review checkout's test class; product source stayed exact. Reproduction patch and source: `monitor-output-fix-codex/reviewer-probes.patch`, `reviewer-probes.inc`. Build output: `windows-probe-build.txt`. Command: `monitor_output_tests.exe reviewerHiddenCanvasMustReleaseShedImage reviewerOldComparisonCannotOverrideNewCompareCommand reviewerInvalidFitMustNotDestroyRememberedFit -o docs/verification/monitor-output-fix-codex/windows-review-probes.txt,txt`.
```
QINFO: reported bytes 6400 hidden secondary bytes 6400
FAIL: hidden secondary image().isNull() returned FALSE
FAIL: old comparison actual QSize(30x30), expected QSize(64x64)
FAIL: reopened fit actual "shrink", expected "fill"
Totals: 2 passed, 3 failed, 0 skipped, 0 blacklisted, 16ms
probe_exit=3
source_diff_exit=0
```
The adjacent raw files contain the full actual output and exact failure locations; the summary above abbreviates log formatting only. Tests were restored byte-for-byte from a pre-probe copy; `git diff --exit-code ae1b7e4 -- src include ui tests CMakeLists.txt` returns 0. The restored target is rebuilt and its CTest result is retained in `windows-restored-build.txt` and `windows-restored-ctest.txt`.

No monitor full Windows or Docker gate was run: independently reproduced source defects already prevent approval. No historical author or integrated-main gate is treated as a fresh result on ae1b7e4. Any later full gate must run through `& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/review-monitor-output-fix -Run { ... }`.

Applied the required application-interface-design and UI foundation/design-review skills to the native presentation surface. Strengths: the output stays free of controls, single-key dismissal has one close path, and fit selection is separated per display id. Highest-leverage fix: make real canvas ownership match the declared memory budget. Offscreen tests do not qualify GPU rendering, mixed DPI/refresh, physical hotplug, ICC/HDR, suspend or device loss. This review changes no production source, shared interface, root configuration, queue or other lane's worktree and performs no merge.

## Restored-target verification

The first post-restore CTest still ran the probe executable and failed (raw `windows-restored-ctest.txt`). Restoring the original file preserved an older source timestamp, so the newer probe object was not recompiled. After advancing only the restored source timestamp and rebuilding (`windows-restored-rebuild.txt`, including compile and link), the exact original suite passed again:
```
Totals: 24 passed, 0 failed, 0 skipped, 0 blacklisted, 159ms
1/1 Test #33: monitor_output ................... Passed 0.24 sec
100% tests passed out of 1
Total Test time (real) = 0.30 sec
```
Actual final files: `windows-restored-cases.txt`, `windows-restored-ctest-final.txt`, `restored-result.txt`. Source diff against ae1b7e4 remains empty. The failed intermediate attempt is retained and is not a product regression claim.
