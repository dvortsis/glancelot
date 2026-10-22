# INPUT-02 preliminary independent review

Candidate: `ddd67ae3fbe004d9bfa3d5d2e296438853fe6145` (`claude/input-02`).
Reviewer: Codex lane, isolated branch `codex/review-input-02`, worktree `.worktrees/codex-review-input-02`, build `build-codex-review-input-02`.

Disposition: **CHANGES REQUIRED — preliminary review, not release approval.**

The candidate is reviewed as an exact snapshot. Its queue still says `ready`, and `docs/verification/INPUT-02.md` does not exist. It predates main's platform-ops integration. This report does not change the queue, contracts or implementation. Independent integration and integrated Windows/Linux checks remain required.

## Findings

Five focused runtime probes reproduced the issues below. The scope probe also established that a pending canvas chord survives transferring keyboard focus to the tree. No implementation fixes were made.

1. **P1 — Accepted native-pane bindings do not execute.** `src/app/viewer_controller.cpp:667`, `ui/PhotoGrid.qml:350` (also tree native-key override in `ui/FolderTree.qml:259`). `setBinding("grid-up", "Right")` succeeds and is explicitly exercised by the new controller tests, but the grid's hard-coded shortcut override consumes Right and moves its cursor before the registered command runs. Likewise, accepted `grid-open = Left, D` cannot start. A menu can advertise Right for Up while Right performs a different action. Resolve registered commands and native pane keys through the same input path, including both chord steps, and cover these exact accepted remaps in UI tests.

2. **P2 — Canvas/grid scopes remain active while the tree owns focus.** `ui/Main.qml:338`. Canvas allowance is only `!gridScope`, and grid allowance is based on content mode rather than the focused region. With a photo open and the tree list focused, R still rotates the photo; similarly a grid-scoped binding can run behind the focused tree in grid mode. The conflict checker nevertheless treats these scopes as disjoint. A separate probe started K, R in the canvas, transferred focus to the tree list, and found chordPending still true. Gate pane scopes from actual pane focus, cancel pending input when its pane loses focus, and retain the looser explicit toolbar route separately. The candidate's added “As delivered” prose claims the tree fallthrough is intentional, but it deviates from the frozen focused-pane resolution and IN-02 no-context-leaks acceptance.

3. **P2 — Pointer cancellation leaves a keyboard hold active.** `ui/Main.qml:691` and `ui/Main.qml:680`. `onCanceled` calls `endPointerLoupe()`, which only restores when `pointerLoupe` is true. A Z keyboard hold leaves that flag false, so a canceled mouse grab does not restore the saved view. IN-05 and the frozen hold contract explicitly require restoration on pointer cancellation. Keep ordinary mouse-release isolation, but make cancellation restore any active hold, and add that missing lifecycle test.

4. **P2 — The tree destructive menu bypasses the registered-command route and parity check.** `ui/FolderTree.qml:440`, `ui/FolderTree.qml:444`; test blind spot at `tests/input_ui_tests.cpp:145`. The retained trash row is a plain MenuItem with its own enablement and direct `deleteRequested(rowMenu.path)` route. Its generic text omits the target name. The new parity helper only visits object names matching `^menuItem-`, excluding this row. A root's trash menu is disabled while the registered Delete command can fall through to a current photo/grid target. This is a retained baseline route gap in the newly claimed F055 scope, rather than a newly introduced deletion regression. Route the row through the target-aware registry predicate and include every actionable row in parity tests.

5. **P2 — Keyboard navigation does not bring the last compact canvas-menu row into view.** `ui/Main.qml:723` and `ui/Main.qml:727`. In a 540x360 window, open the canvas menu and press Down until the destructive last item is current. `menu.currentIndex` reaches 13, but that row's scene rect remains `(168,347,360,32)`, extending below the 360px window. The menu background stays bounded, so the existing `insideWindow` test passes while the row and its target text are cut off. An initial End-key probe was ambiguous; this finding uses the confirmed Down-key follow-up. Ensure current-item changes scroll the content view to contain the row and test actual row geometry, not only the popup background.
## Scope and design review

Read AGENTS.md, the frozen INPUT-02 packet, lanes plan, Codex handoff, requirements, application-interface-design, layout grids/spacing, typography, geometry, iconography, motion, responsive-layout and design-review skills. Applied these to native Qt/QML and existing Theme tokens.

Strengths: reusable command-menu rows retain the Theme surface/type/geometry system; live bindings are aligned separately from labels; grid destructive labels name the target; compact canvas menus bound their outer geometry and provide a scrollable list. Highest-leverage fix: unify accepted registered bindings with the native pane key handlers.

Software-rendered dark/light and compact/wide captures were generated. Visually inspected compact dark canvas and wide light grid images. Captures preserve the branding and readable shortcut-label hierarchy. Software rendering reports `No QRhi found ... QQuickRhiItem will not be functional`; these captures do not qualify GPU rendering. Physical trackpads, keyboard layouts/IMEs and assistive technology remain unverified.

## Verification

Commands ran from the isolated review worktree with Qt 6.8.3 MSVC bin and project-local Python Scripts on PATH.

```text
./scripts/build.ps1 -BuildDir build-codex-review-input-02
[199/199] Linking CXX executable platform_integration_tests.exe
exit 0

ctest --test-dir build-codex-review-input-02 --output-on-failure
 8/15 Test #8: window_slots ... Passed 34.94 sec
 9/15 Test #9: folder_grid ... Timeout 180.01 sec
10/15 Test #10: input_ui ... Passed 21.16 sec
15/15 Test #15: platform_integration ... Failed 0.19 sec
87% tests passed, 2 tests failed out of 15
Total Test time (real) = 344.56 sec
```

The timeout occurred while other lanes were running broad grid suites; the lead reported equivalent contention/timeouts. This run remains incomplete verification: no timeout was relaxed and the full suite was not restarted. The platform failure is the known unrelated baseline predating current main's platform-ops fix, confirmed by an isolated log capture:

```text
./build-codex-review-input-02/platform_integration_tests.exe -o .cache/platform-review.txt,txt
FAIL! PlatformIntegrationTests::openTerminalInvalidOrMissingPathReturnsFalse()
'!glancelot::PlatformIntegration::openTerminal("C:/nonexistent_dir_path_12345")' returned FALSE
Totals: 4 passed, 1 failed, 0 skipped, 0 blacklisted, 71ms
```

A preserved executable from the unmodified candidate was also run with file output to obtain the QtTest case summary (the Windows GUI executables produced no console text in CTest):

```text
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
./build-codex-review-input-02/input_ui_candidate.exe -o .cache/input-candidate-targeted.txt,txt
Totals: 11 passed, 0 failed, 1 skipped, 0 blacklisted, 26298ms
```

The skipped case is opt-in reviewCaptures. It was run separately on the unmodified executable with GLANCELOT_REVIEW_CAPTURE=1:

```text
./build-codex-review-input-02/input_ui_tests.exe reviewCaptures -o .cache/input-review-captures.txt,txt
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 6337ms
```

Review probes temporarily added QtTest slots to this worktree's existing harness. Production sources remained exact. They used generated disposable photos and opened deletion confirmation only; no source media was deleted. The first run included five direct behavioral probes plus the exploratory End-key compact-menu probe:

```text
./build-codex-review-input-02/input_ui_tests.exe reviewRemappedGridNativeKey reviewCanvasScopeDoesNotLeakIntoTree reviewChordCancelsOnPaneFocusLoss reviewHoldCancelsOnPointerCancel reviewRootTreeMenuMatchesDeleteKey reviewCompactMenuLastRowReachable -o .cache/input-review-probes.txt,txt
FAIL! reviewRemappedGridNativeKey: Right accepted for grid-up but directory did not change
FAIL! reviewCanvasScopeDoesNotLeakIntoTree: rotation actual90 expected0
FAIL! reviewChordCancelsOnPaneFocusLoss: pending canvas chord survived tree focus
FAIL! reviewHoldCancelsOnPointerCancel: MouseArea canceled signal left keyboard hold active
FAIL! reviewRootTreeMenuMatchesDeleteKey: disabled root trash menu, Delete opened confirmation
FAIL! reviewCompactMenuLastRowReachable: exploratory End-key check (not relied upon)
Totals: 2 passed, 6 failed, 0 skipped, 0 blacklisted, 10614ms
```

The compact-menu probe was changed to actual Down-key navigation until currentIndex equals the last item's index; no production change was made:

```text
./build-codex-review-input-02/input_ui_tests.exe reviewCompactMenuLastRowReachable -o .cache/input-review-compact-followup.txt,txt
FAIL! reviewCompactMenuLastRowReachable
Last row rect: 168,347 360x32 currentIndex=13
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 2542ms
```

The test source was restored byte-for-byte from its pre-probe copy; `git diff --exit-code -- tests/input_ui_tests.cpp` returned 0. The normal input_ui_tests executable was restored from the preserved candidate executable. Only this review report is committed. Probe source/logs and screenshots remain ignored under `.cache`; the modified probe executable is retained as `build-codex-review-input-02/input_ui_review_probes.exe` for local reproduction.

No duplicate Linux build was run because the integrator had Linux checks active. There is no candidate Linux evidence in this report, no integrated-commit qualification, and no release approval. Required next steps are Claude fixes in its lane, verification documentation, independent re-review and serial integration under the lead's lock.
