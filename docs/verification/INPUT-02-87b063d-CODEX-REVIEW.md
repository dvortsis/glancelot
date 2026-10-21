# INPUT-02 pinned re-review

Candidate: `87b063d1523808c9c7dc28a53f2030dd989c01c4` (`claude/input-02`). Reviewer: Codex lane; isolated `codex/review-input-02-r2`, `.worktrees/codex-review-input-02-r2`, build `build-codex-review-input-02-r2`. Rules read from `10bdeb1:AGENTS.md`; no main merge. Compared the exact fix against `a6bda32` and `docs/verification/INPUT-02-FINAL-CODEX-REVIEW.md`.

Disposition: **CHANGES REQUIRED**. Runtime results are recorded below when completed; no integration approval.

## Remaining finding

**P2 — Tree Trash still bypasses registered-command routing and the shared enablement predicate.** `ui/FolderTree.qml:446-453` keeps `folderMoveToTrash` as a plain MenuItem with its own `depth > 0 && !fileOperations.busy` predicate and direct `panel.deleteRequested(rowMenu.path)` signal. It has no commandId. `tests/input_ui_tests.cpp:157-160` only enumerates rows named `menuItem-*`, so the stated every-row parity check still omits Trash. The new root guard in `ui/Main.qml:478-480` fixes the dangerous root-to-photo retargeting, but it does not satisfy the frozen F055 requirement that every actionable menu row be a registered command with the same eligibility and target logic as its keyboard path. On a root, scopeAllows(delete-selection) remains true while the menu is disabled; during a busy operation the row's additional predicate also differs. Route both through one target-aware command/predicate and include this row in parity coverage. Keep the root safety guard.

## Assessment of the prior five findings

1. Native pane remaps: grid keyAction now yields to registered grid/tree keys, including matching chord steps; tree ShortcutOverride uses the same resolver. Native keys still outrank workspace/global commands, consistent with the retained pane-precedence rule. The focused real-event regression passed (see windows-fix-cases.txt).
2. Tree ownership: scopeAllows now excludes tree focus for canvas/grid commands, and onFocusRegionChanged cancels pending chords. The focused real-event regression passed (see windows-fix-cases.txt).
3. Pointer cancellation: MouseArea.onCanceled now calls canvas.endLoupe in addition to ending the pointer hold; endLoupe restores saved zoom/pan/fit and removes the key-release filter. The focused real-event regression passed (see windows-fix-cases.txt).
4. Tree Trash: root retargeting fixed; the registration/shared-predicate subfinding above remains.
5. Compact menus: ListView positions the current index with Contain on keyboard selection. The focused 540x360 containment regression passed (see windows-fix-cases.txt).

Product source is unchanged. Actual pin/diff/line output: `input-02-r2/source-proof.txt`. Actual build output: `input-02-r2/windows-build.txt`. Actual full-gate output: `input-02-r2/windows-full.txt`. Full gates use the main checkout's machine-local gate-lock script, in the same PowerShell process as the gate.

Applied application-interface-design, layout-grids, layout-spacing, typography-ui, component-geometry, iconography-ui, motion-ui, responsive-layout and design-review to native Qt/QML and existing Theme tokens. Strengths: focus ownership is explicit, cancelled holds restore the saved view, and menu navigation has a containment path. Highest-leverage remaining fix: unify the tree destructive command with its keyboard route and eligibility.

Physical trackpads, layouts/IMEs, assistive technology, mixed-DPI displays and real GPU presentation remain unverified. Prior decoder-only approval on `db23447` is historical independent evidence for byte-identical worker code, not a newly repeated cancellation experiment. No shared headers, production source, root configuration, work queue, other lane worktrees, or main were changed by this review.

## Fresh Windows results

Build-only command (exit 0, 199/199 steps):
`& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/review-input-02-r2 -Run { & ./scripts/build.ps1 -BuildDir build-codex-review-input-02-r2 }`

Targeted command: `ctest --test-dir build-codex-review-input-02-r2 -R '^input_ui$' -V` (Qt bin on PATH). Actual output in `input-02-r2/windows-input-ui.txt`:
```
1/1 Test #10: input_ui .........................   Passed   37.54 sec
100% tests passed out of 1
Total Test time (real) = 37.62 sec
```

Direct QtTest command (offscreen/software, Qt bin on PATH):
`input_ui_tests.exe remapsBeatNativePaneKeysAndTreeOwnershipGatesCanvasKeys cancelledPointerGrabRestoresAKeyHold treeDeleteKeyMatchesItsMenuOnARoot compactMenusScrollToTheCurrentRow -o docs/verification/input-02-r2/windows-fix-cases.txt,txt`
```
PASS   : InputUiTests::remapsBeatNativePaneKeysAndTreeOwnershipGatesCanvasKeys()
PASS   : InputUiTests::cancelledPointerGrabRestoresAKeyHold()
PASS   : InputUiTests::treeDeleteKeyMatchesItsMenuOnARoot()
PASS   : InputUiTests::compactMenusScrollToTheCurrentRow()
Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 9925ms
```
The output also records No QRhi warnings. These are input/state tests, not GPU rendering qualification. No source probe or test modification was required.

Full Windows command: `& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/review-input-02-r2 -Run { & ./scripts/build.ps1 -BuildDir build-codex-review-input-02-r2 -Test }`. At initial handoff preparation it was waiting for the `claude/input-02-mainsync` gate; see actual waiting output in `input-02-r2/windows-full.txt`. Pending is not passed. No fresh Linux Docker gate has been run for this review; the remaining source finding prevents packet approval regardless.

The committed initial handoff retains the actual queued-gate output in input-02-r2/windows-gate-waiting.txt. The live windows-full.txt will be retained with its completed outcome in a follow-up evidence commit.

## Completed pinned Windows full gate

The queued full gate acquired the machine lock and completed on unchanged product/test source from87b063d:
```
window_slots Passed 35.43 sec
folder_grid Passed 174.20 sec
input_ui Passed 32.40 sec
platform_integration Failed 0.27 sec
93% tests passed, 1 tests failed out of 15
Total Test time (real) = 398.24 sec
```
Actual full command output is now retained in `input-02-r2/windows-full.txt`. This is14/15, not a full pass. A fresh direct platform run with Qt bin on PATH and an awaited GUI-process pipeline records the exact assertion in `windows-platform-awaited.txt`: openTerminal("C:/nonexistent_dir_path_12345") incorrectly returned true; 4 QtTest rows passed and1 failed. The prior immediate GUI launch's output is also retained in windows-platform-cases.txt; its log was initially still open when read, so only the awaited run's exit status is used.

`git diff --exit-code 76da49e 87b063d -- src/app/platform_integration.cpp tests/platform_integration_tests.cpp` returns0: those inputs are unchanged from the packet base. The author previously reported the same baseline assertion. This review did not run a new full baseline suite. The pinned snapshot was not merged with main and no timeout, assertion, renderer setting or source was changed to hide the failure. Remaining tree Trash finding and CHANGES REQUIRED disposition are unchanged. Linux qualification remains unexecuted for this pinned review.
