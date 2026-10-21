**Verdict: ACCEPT.** There are no required corrections. I'm giving one non-blocking hardening note and a few polish items below. This is a read-only source and skill review. It is not integrated full-gate, platform or hardware approval.

**Skills read this round (full text, via Read):** application-interface-design, layout-grids, layout-spacing, typography-ui, component-geometry, iconography-ui, motion-ui, responsive-layout and design-review. All nine are under `C:/Users/dvort/.agents/skills/ui-design-skills/skills/<name>/SKILL.md`.

**Not read this round:** I did not re-read `docs/INPUT-02-PLAN.md` or the 87b063d Codex review in full. I read `ui/FolderTree.qml`, `ui/CommandMenuItem.qml`, the relevant `ui/Main.qml` ranges and `folder_tree_model.hpp`. I only grepped `correction-final.patch` for its key assertions, and I did not read `tests/` directly. Treat the plan-parity and test-adequacy parts as lighter-weight.

## Findings against your focus areas

- **Keyboard and menu target parity: OK.**
  - The Delete key and both menu rows go through `delete-selection` (`ui/Main.qml:487-492`, `ui/FolderTree.qml:445-453`).
  - The target is chosen at execution time by `treePanel.currentSelectedFolder()`.
  - The row menu sets `list.currentIndex = row` before popup (`ui/FolderTree.qml:31`), so the menu and the key resolve the same row.
  - `currentSelectedFolder` also accepts `rowMenu.visible`, so focus held by the popup doesn't change the target.
- **Eligibility parity: OK.**
  - `commandBlockedReason` is the single source. `scopeAllows`, `routeAllows`, `routeBlockedReason`, the menu label and the execution path all call it (`ui/Main.qml:335-366`, `ui/Main.qml:488`).
  - The execution path re-checks it and shows a notice instead of acting. That is the fresh guard you mentioned.
- **Destructive labels: OK.**
  - The label names the target ("Move “name” to the Recycle Bin/Trash"), is truncated by `rowMenu.name`, and uses the danger colour together with the trash icon and text.
  - Meaning is not carried by colour alone, which satisfies the application-interface-design and design-review rules.
  - The grid menu follows the same pattern with a singular and a count form.
- **Root and busy disabled feedback: OK.**
  - The row stays visible and the label becomes the reason ("Drives can't be moved…", "Wait for the current move…"). That follows the "make disabled states understandable" rule.
  - Disabled opacity uses `Theme.disabledOpacity`, and the disabled row stays legible.
- **Focus ownership: OK.**
  - `handedOff` stops the menu from stealing focus back from the confirmation dialog.
  - `focusedControls` is incremented on open and decremented on close, so the HUD stays open.
  - `focusRegion` ignores popups and dialogs.
- **Compact menu reachability: OK.**
  - `rowMenu` has two rows, and the unavailable row takes no space.
  - Its width is `implicitWidth: 232`, matching the compact panel width, and the label elides.
  - `openRowMenuAtCursor` scrolls the cursor row into view before it anchors the popup.
  - I did not check popup placement at the extreme narrow window sizes.
- **Component reuse, geometry and iconography:**
  - `CommandMenuItem` is one canonical primitive with finite semantic variants (`danger`, `available`, `iconName`).
  - Radius, border, height and gap values all come from Theme tokens (`radiusControl`, `radiusSmall`, `controlHeight`, `gapXs`, `gapSm`, `textBody`, `textSmall`, `monoFamily`).
  - The icon is the existing `UiIcon`, so there is no second family.
  - The key hint uses the mono family, as typography-ui recommends.
  - The menu adds no motion.
  - No web framework or new styling was introduced.

## `cursorIsRoot()` and non-notifying `rowInfo`

`rowInfo()` is a plain `Q_INVOKABLE` with no change signal. The binding only re-evaluates when `list.currentIndex`, `focusRegion`, `fileOperations.busy` or another tracked property changes. The `FolderTreeModel` signals are `rootChanged`, `currentRowChanged`, `countChanged` and `loadFinished`.

- **What can go stale:** the `allowed` binding in `CommandMenuItem` can be stale if the same index holds a different row. That needs a topology change that leaves `currentIndex` unchanged, for example the current row being removed by a collapse that doesn't move the cursor, or a reset. Inserts or removals above the cursor shift `currentIndex` in `ListView`, and that re-fires the binding. I believe this from Qt's `ListView` behaviour and did not verify it empirically.
- **Visible symptom:** the label binding also depends on `rowMenu.name`, so it refreshes when the path changes. That makes a label and enabled-state mismatch possible. For example, a "Move X…" label could show on a row that is disabled, or the reverse.
- **Safety:** this is a truthfulness glitch in a narrow edge case, not a safety defect. Key resolution calls `scopeAllows` fresh, and the execution guard re-checks, so a stale-enabled root row ends in a notice or a no-op and never acts.
- **Classification:** non-blocking hardening. It is not a required correction.
- **Optional fix:** make the dependency explicit. `cursorIsRoot()` could read `folderTree.count` and `folderTree.currentRow`, or a `revision` property bumped from `countChanged`, `rootChanged` and `loadFinished` and on `rowMenu.onAboutToShow`.

## Tests

- **Delegate wait:** I accept your acknowledged advisory about the delegate-wait count of 3 masking a missing delegate. `titleMoved 2of4` and the backend outcomes remain the real checks. A logical assertion that the failures model has size 2 would be a reasonable, cheap addition, but I don't require it. I'm not asking for an artificial sleep.
- **Busy case:** the patch exercises the busy row. It holds the operation with a gate, opens `folderRowMenu`, closes it, then waits for the operation to finish and for `delete-selection` to become allowed again.
- **Not verified:** I did not verify the pass/fail results of any test, build or run.

## Non-blocking polish

- The `cursorIsRoot()` dependency hardening above.
- If `invoke()` is reached with a stale-enabled blocked command, `routeAllows` returns silently. Showing the same notice the execution path uses would be more truthful.
- Add the `size == 2` failures-model assertion in the test if you want it.

**Strengths:**
- One shared eligibility predicate.
- Target-naming destructive labels.
- A reusable `CommandMenuItem` built only from Theme tokens.

**Highest-leverage change:** the `cursorIsRoot()` revision dependency.