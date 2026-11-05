# PR 21 review: closeout notes (`lead/closeout-notes`)

**Verdict: CHANGES REQUIRED**

- PR: 21, `docs(contracts): sharpen the EN-07 rule and the asset collision wording`, base `main`.
- Head reviewed: `61518ac5c61afdac4b3a9ca9868fea421210eba8` (matches `gh pr view 21 --json headRefOid`).
- Base: `origin/main` at `2e033b69202fa2b71589bdcc4825e4f7d15d082c`. Diff: `git diff origin/main...61518ac`, four files, 4 insertions and 4 deletions (`AGENTS.md`, `docs/RELEASING.md`, `docs/WORK-QUEUE.yaml`, `docs/contracts/ASSET-GROUP.md`).
- Responds to non-blocking notes 1 to 6 of `docs/verification/PR20-CLOSEOUT-REVIEW.md` (`origin/claude/review-pr20`).
- Reviewer: fresh context, wrote none of this. No build was run. Checkout: detached worktree `.worktrees/review-pr21` at the head.

## Findings

### Blocking

1. **AGENTS.md, EN-07 rule: the new ordering sentence is wrong, measured on Qt 6.8.3.**
   - The PR says: "Handler order follows connection order, so a handler that connects later (in a `Loader` item, a dynamically created object, or a `Connections` whose `target` or `enabled` changes) runs before the loupe ends."
   - The first half is true and contradicts the second. Main's last-declared `Connections` connects *first* (parser-status callbacks pop LIFO, `EN-07-REVIEW.md` line 19). Handlers run in connection order. So a handler that connects later runs *after* Main's handler, which means after `endLoupe`.
   - I measured it with `qml.exe` from `.tools/Qt/6.8.3/msvc2022_64`, on a QML signal and on a C++ signal (probes below). A `Loader` item's handler, a `createObject` result's handler, and a `Connections` whose `target` was rebound all ran after the last-declared handler. Toggling `enabled` off and on did not move a handler at all.
   - PR 20's note 1 asked to lead with the certain reason and call the ordering hazard unverified. This PR keeps the hazard and states it as fact, with a premise that disproves it. AGENTS.md is what every session follows, so a false mechanism there is not wording polish.
   - The prescription itself is right: handle a lazily loaded owner's command on the `Loader` in `Main.qml`. The sentence "A handler inside a component that isn't loaded yet misses the command that would create it" is correct and is the real reason.
   - Suggested text (any equivalent is fine): "The `Connections` that calls `handleViewCommand` is declared last. QML connects a file's `Connections` in reverse declaration order and calls handlers in connection order, so Main's handler runs first and the loupe ends before a component's handler acts (`loupeEndsBeforeAMovedHandlerActs`). A handler that connects later (a `Loader` item, a `createObject` result, a `Connections` whose `target` is rebound) runs after all of them. A handler inside a component that isn't loaded yet misses the command that would create it, so a command whose owner loads lazily is handled on the `Loader` in `Main.qml`, as `export-selection` is."

### Non-blocking

2. **AGENTS.md: the list of command families Main keeps is still partial.** `handleViewCommand` on `origin/main` (`ui/Main.qml` lines 751-829) also handles the dialog ids (`open`, `palette`, `settings`, `shortcuts-help`), the view-mode ids (`grid`, `view-preview`, `view-grid`, `view-combined`, `combined-orientation`, `focus-mode`), the slideshow ids (`slideshow`, `slideshow-slower`, `slideshow-faster`), `navigator`, `copy-image` and `quit`. "canvas, window, layout, monitor, workspace and reference" covers some of these only loosely. Adding "view-mode, dialog and slideshow", or saying "and every command no component owns", would be exact. The singular-connection wording from PR 20 note 2 is fixed ("The `Connections` that calls `handleViewCommand`").
3. **ASSET-GROUP.md: the example names match B1.** `buildTransferPlan` (`origin/claude/b1-asset-services:src/app/copy_move_service.cpp` lines 583-676) gives a multi-member asset one shared `n`, starting at 2, formatted `"%1 (%2)%3"` from `splitAssetName`. `splitAssetName` (`asset_group.cpp` lines 85-96) splits a FullName sidecar `IMG.CR2.xmp` into `IMG` + `.CR2.xmp`. So `IMG (2).CR2`, `IMG (2).jpg` and `IMG (2).CR2.xmp` are what B1 produces. B1's handoff (`origin/claude/b1-asset-ui:docs/handoff/B1-HANDOFF.md`, decision 2) says the same. The new "These are not grouping `Collision` groups" sentence resolves PR 20 note 3, and the concrete names resolve note 4.
   - Detail, no change needed: a single-file asset keeps `enqueueTransfer`'s `completeBaseName` naming (line 640), and "taken" also counts names this plan already gave out (line 628), not only names on disk. Neither contradicts the text.
4. **WORK-QUEUE.yaml B2 note: the hazard is real.** The contract's Overwrite paragraph says "The decision applies only to members that collide." Take a destination holding `IMG.CR2` and `IMG.CR2.xmp`, and a source asset `IMG.CR2` + `IMG.jpg`. Only `IMG.CR2` collides, so Overwrite replaces it and copies `IMG.jpg`, and the old `IMG.CR2.xmp` stays beside a different photo. That breaks the block's own invariant "A sidecar never lands beside a different photo of the same name." The note names it with a concrete example and leaves the decision and a test to B2's brief, as PR 20 note 5 asked.
5. **RELEASING.md command: runs as written in PowerShell 7.6.9 and Git Bash.** The `git show ... > <temp file>` copy is byte-identical to `origin/main`'s file after line-ending normalisation (the script is pure ASCII, so PowerShell's re-encoding can't damage it). A clean range exits 0 and a bad subject exits 1 from both shells (output below). The `origin/<parent branch>` form from PR 20 note 6 is adopted. PR 20 note 7 (dates) and note 8 (copy) were outside this PR's scope.
6. **Guard passes:** `pr_guard: code=False, 0 error(s)`, exit 0.

## Command output

### Check 1: the order probe

`main.qml` (with `Late.qml` holding one `Connections` on `src` that logs `loader-item`):

```qml
import QtQuick
Item {
    id: root
    QtObject { id: bus; signal fire(string tag) }
    Connections { target: bus; function onFire(tag) { console.log("ORDER first-declared", tag) } }
    Loader { id: lazy; active: false; source: "Late.qml"; onLoaded: item.src = bus }
    Connections { id: toggled; target: bus; enabled: true; function onFire(tag) { console.log("ORDER toggled", tag) } }
    Component { id: dyn; Item { Connections { target: bus; function onFire(tag) { console.log("ORDER createObject", tag) } } } }
    Connections { target: bus; function onFire(tag) { console.log("ORDER last-declared(main)", tag) } }
    Component.onCompleted: {
        bus.fire("static")
        lazy.active = true
        dyn.createObject(root)
        toggled.enabled = false; toggled.enabled = true
        bus.fire("after-late")
        Qt.quit()
    }
}
```

Run 1, toggling `enabled` (`QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 qml.exe main.qml`):

```text
qml: ORDER last-declared(main) static
qml: ORDER toggled static
qml: ORDER first-declared static
qml: ORDER last-declared(main) after-late
qml: ORDER toggled after-late
qml: ORDER first-declared after-late
qml: ORDER loader-item after-late
qml: ORDER createObject after-late
```

Run 2, the same with `toggled.target = null; toggled.target = bus` instead:

```text
qml: ORDER last-declared(main) static
qml: ORDER toggled static
qml: ORDER first-declared static
qml: ORDER last-declared(main) after-late
qml: ORDER first-declared after-late
qml: ORDER loader-item after-late
qml: ORDER createObject after-late
qml: ORDER toggled after-late
```

Static handlers run in reverse declaration order, as `EN-07-HANDOFF.md` measured. Every later connection runs after the last-declared handler. Run 3 repeats it on a C++ signal, since `viewer.viewCommand` is one: `bus` is an `Item`, every handler is `function onWidthChanged()` (`QQuickItem::widthChanged`), and the probe sets `bus.width = 1`, then makes the late connections (Loader item, `createObject`, a `target` rebound to another item and back, `enabled` toggled), then sets `bus.width = 2`:

```text
qml: ORDER last-declared(main) 1
qml: ORDER toggled 1
qml: ORDER rebound 1
qml: ORDER first-declared 1
qml: ORDER last-declared(main) 2
qml: ORDER toggled 2
qml: ORDER first-declared 2
qml: ORDER loader-item 2
qml: ORDER createObject 2
qml: ORDER rebound 2
```

Same order on the C++ path: every later connection runs after the last-declared handler, and toggling `enabled` doesn't move a handler. This was not run against `viewer` inside `Main.qml` itself.

### Check 4: the RELEASING.md command

PowerShell 7.6.9:

```text
> git show origin/main:scripts/ci/commit_lint.py > <scratch>\commit_lint_ps.py
> python <scratch>\commit_lint_ps.py --range origin/main..origin/lead/closeout-notes
exit=0
> python <scratch>\commit_lint_ps.py --range origin/main~5..origin/main
exit=0
identical-after-eol-normalize: True
nonascii-in-source: 0
```

Git Bash, plus a bad subject on an unreferenced commit object (`git commit-tree`, dated 2026-11-07, no ref moved):

```text
$ python $S/commit_lint_bash.py --range origin/main..origin/lead/closeout-notes
bash clean exit=0
bad=db1041a655bd472b7d7361251c71119c6710c914
$ python $S/commit_lint_bash.py --range origin/main..db1041a
commit_lint: commit db1041a65: 'bad subject without type': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
bash bad exit=1
(PowerShell copy, same range)
commit_lint: commit db1041a65: 'bad subject without type': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
ps-copy bad exit=1
```

### Check 5: guard

```text
> git worktree add --detach .worktrees/review-pr21 61518ac5c61afdac4b3a9ca9868fea421210eba8
> git rev-parse HEAD
61518ac5c61afdac4b3a9ca9868fea421210eba8
> python scripts/ci/pr_guard.py --base origin/main --head-ref lead/closeout-notes --head 61518ac5c61afdac4b3a9ca9868fea421210eba8
pr_guard: code=False, 0 error(s)
guard exit=0
```

## To approve

Fix finding 1 (replace the ordering sentence so it matches the measured order). Findings 2 to 6 need nothing. A fresh reviewer re-reviews the new head.
