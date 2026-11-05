# PR 21 review, round 2: closeout notes (`lead/closeout-notes`)

**Verdict: APPROVED**

- PR: 21, `docs(contracts): sharpen the EN-07 rule and the asset collision wording`, base `main`.
- Head reviewed: `29cdce4a64c864fe3220801f163059b058a4de04` (matches `gh pr view 21 --json headRefOid`).
- Base: `origin/main` at `2e033b69202fa2b71589bdcc4825e4f7d15d082c`. Diff: `git diff origin/main...29cdce4`, four files, 4 insertions and 4 deletions (`AGENTS.md`, `docs/RELEASING.md`, `docs/WORK-QUEUE.yaml`, `docs/contracts/ASSET-GROUP.md`). Round 2 delta `61518ac..29cdce4`: `AGENTS.md` only, one line.
- Round 1: `docs/verification/PR21-CLOSEOUT-NOTES-REVIEW.md` (this branch), CHANGES REQUIRED on finding 1.
- Reviewer: fresh context, wrote none of this and not round 1. No build was run. Checkout: detached worktree `.worktrees/review-pr21-r2` at the head, removed afterwards.

## Findings

### Blocking

None. Round 1 finding 1 is fixed.

### Non-blocking

1. **AGENTS.md, EN-07 ordering sentence: accurate, and the false claim is gone.** The new text says `Connections` handlers in one file run in reverse declaration order, so the `Connections` calling `handleViewCommand` is declared last and the loupe ends before a component's handler runs. That matches:
   - `ui/Main.qml` on `origin/main`: the `Connections { target: viewer; function onViewCommand(id) { app.handleViewCommand(id) } }` is the last child of the window (line 2244), its comment says the same, and `handleViewCommand` starts with `canvas.endLoupe()` (line 752).
   - `tests/command_handlers_ui_tests.cpp` `loupeEndsBeforeAMovedHandlerActs` (line 416), whose rows cover PhotoGrid, FolderTree, TransferHud, DeleteFlow, RenameFlow and the `exportFlow` Loader, and which checks the loupe is already off at the handler's first effect.
   - Round 1's probes (static handlers in reverse declaration order) and my probe below.
   - The sentence claiming a later-connected handler runs before the loupe ends is removed, not reworded. Nothing now states a mechanism the measurements contradict. The remaining reason for the `Loader` rule ("A handler inside a component that isn't loaded yet misses the command that would create it") is correct.
2. **Wording, optional: "Within one file" is narrower than the fact the conclusion needs.** The components' handlers live in other files (`PhotoGrid.qml`, `DeleteFlow.qml` ...), so "within one file" does not by itself explain why Main's handler runs before theirs. The measured fact is wider: the order is reverse declaration order across the whole tree created with `Main.qml`, nested component files included (probe below: a child component's inner handlers run at its declaration position, in reverse). Something like "Across `Main.qml` and the components it declares, `Connections` handlers run in reverse declaration order" would be exact. The conclusion is right either way and the test guards it.
3. **Main's command list:** "the commands that act on the canvas, the window or Main's own dialogs and modes. `handleViewCommand` is the list." resolves round 1 finding 2.
4. **Rest of the diff:** `ASSET-GROUP.md`, `WORK-QUEUE.yaml` and `RELEASING.md` are unchanged since round 1 (`git diff --stat 61518ac 29cdce4` shows `AGENTS.md` only), and round 1 checked them against B1's code and both shells. Reread quickly; nothing wrong.
5. **Guard passes:** `pr_guard: code=False, 0 error(s)`, exit 0.

## Command output

### Check 1: order across component files

`Child.qml`:

```qml
import QtQuick
Item {
    property var src
    property string tag
    Connections { target: src; function onWidthChanged() { console.log("ORDER child-" + tag + "-inner-first") } }
    Connections { target: src; function onWidthChanged() { console.log("ORDER child-" + tag + "-inner-last") } }
}
```

`main.qml`:

```qml
import QtQuick
Item {
    id: root
    Item { id: bus }
    Connections { target: bus; function onWidthChanged() { console.log("ORDER main-first-declared") } }
    Child { src: bus; tag: "A-before" }
    Connections { target: bus; function onWidthChanged() { console.log("ORDER main-middle") } }
    Child { src: bus; tag: "B-before" }
    Connections { target: bus; function onWidthChanged() { console.log("ORDER main-last-declared") } }
    Component.onCompleted: { bus.width = 1; Qt.quit() }
}
```

`QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 .tools\Qt\6.8.3\msvc2022_64\bin\qml.exe main.qml` (C++ signal `QQuickItem::widthChanged`, as `viewer.viewCommand` is a C++ signal):

```text
This plugin does not support createPlatformOpenGLContext!
qml: ORDER main-last-declared
qml: ORDER child-B-before-inner-last
qml: ORDER child-B-before-inner-first
qml: ORDER main-middle
qml: ORDER child-A-before-inner-last
qml: ORDER child-A-before-inner-first
qml: ORDER main-first-declared
```

Reverse declaration order holds within a file and across the component files it declares, so Main's last-declared handler runs first. Not run against `viewer` inside `Main.qml` itself; the UI test covers that.

### Check 3: guard

```text
> git worktree add --detach .worktrees/review-pr21-r2 29cdce4a64c864fe3220801f163059b058a4de04
HEAD is now at 29cdce4 docs(contracts): correct the handler-order sentence in the EN-07 rule
> git rev-parse HEAD
29cdce4a64c864fe3220801f163059b058a4de04
> python scripts/ci/pr_guard.py --base origin/main --head-ref lead/closeout-notes --head 29cdce4a64c864fe3220801f163059b058a4de04
pr_guard: code=False, 0 error(s)
guard exit=0
> gh pr view 21 --json headRefOid,title
29cdce4a64c864fe3220801f163059b058a4de04 docs(contracts): sharpen the EN-07 rule and the asset collision wording
```
