# PR 20 review: closeout of EN-07 and PR 19, asset confirmation rules

**Verdict: APPROVED**

- **PR:** 20, `docs(queue): close EN-07 and PR 19, record the asset confirmation rules`, branch `lead/closeout-2026-11-06`.
- **Reviewed head:** `398fb1ae4c82854c29a14c3fea037220414f8d95`, the same as the PR's `headRefOid`.
- **Base:** `origin/main` at `30803be00d806f7b7c62fa555499cf32c4375be1`. The diff reviewed is `git diff origin/main...398fb1a`: 28 files, 7,589 insertions and 2 deletions, all docs and evidence.
- **Reviewer:** a fresh-context Opus subagent that implemented none of this. It worked in a detached worktree `.worktrees/review-pr20` at `398fb1a`, and ran no builds, as the brief asked.

Nothing blocks. Every finding below is non-blocking, and most of them are wording.

## Findings

### Blocking

None.

### Non-blocking

1. **AGENTS.md, EN-07 rule: the reason given for the Loader is not the certain one.**
   - The rule says a handler in a `Loader` item "connects later and would run before the loupe ends". That comes from `EN-07-REVIEW.md` line 21.
   - Line 19 of the same review gives the mechanism: the last-declared `Connections` connects first, and endpoints are called in connection order. On that premise, a handler that connects later runs *after* Main's, so after the loupe ends.
   - The measured order in `EN-07-HANDOFF.md` (lines 60-70) fits either model. It shows execution in reverse declaration order, but nothing measured a lazily created handler.
   - The certain reason is in the handoff (lines 74 and 274): `ExportDialog.qml` doesn't exist until the first request, so a handler inside it would miss the command that creates it.
   - The prescription is right either way, because the Loader's own `Connections` is created with `Main.qml`. Suggest leading with "it would miss the command that creates it", and calling the ordering hazard unverified.
2. **AGENTS.md, EN-07 rule: two wording gaps.**
   - "`ui/Main.qml` keeps `invoke`, scope routing and canvas and window commands" leaves out the layout and reference ids that `handleViewCommand` still handles: `arrange-panes`, `layout-lock`, `layout-reset`, `layout-preset-next`, `reference-pin` and `reference-close`. The handoff's draft says "canvas, window and layout ids".
   - "Main's own `viewCommand` connection" is singular. Main has three: the slideshow block (line 707), the `exportFlow` Loader (line 2199) and `handleViewCommand` (line 2244). The rule means the last one, so "Main's `handleViewCommand` connection" would be exact.
   - The EN-07 reviewer also named two other late-connecting cases: a `Connections` whose `target` is rebound, and one that toggles `enabled`. The rule leaves them out.
3. **The ASSET-GROUP "Lead decisions" block: "collision" means two things.**
   - Just above the new block, "Collision members are excluded and the operation continues with the rest" is about same-stem *grouping* collisions (`GroupFlag` `Collision`).
   - The new bullet "A member's collision moves the whole asset" is about a name that already exists in the *destination*.
   - Read side by side, the two can look contradictory. Suggest "A member's destination-name collision moves the whole asset".
4. **The ASSET-GROUP "Lead decisions" block: "stem" is used for the asset, not the file.**
   - The contract defines the stem of `IMG.CR2.xmp` as `IMG.CR2`. The new bullet's `stem (n).CR2.xmp` uses "stem" for the asset's stem (`IMG`).
   - B1's code does the right thing: `splitAssetName` gives `IMG (2).CR2.xmp`. Suggest "the asset's stem" or the concrete `IMG (2).CR2` and `IMG (2).CR2.xmp`.
5. **B2 queue note: right, but it doesn't name the hazard.**
   - `lead_note_b1` says B2's Overwrite "still applies only to members that collide", which matches the contract's Overwrite paragraph.
   - The hazard it leaves to B2's brief: Overwrite replaces only the colliding members. A non-colliding old companion in the destination (for example an existing `IMG.xmp` or `IMG.JPG` of a different photo) then stays beside the new `IMG.CR2`. That is exactly the "sidecar beside a different photo" outcome the whole-asset rule forbids.
   - Worth stating in B2's brief when it is written.
6. **RELEASING.md command: runnable, but only on this PC.**
   - It works as written from PowerShell; see the output below. It fails a bad subject (exit 1) and passes a clean range (exit 0).
   - `C:\src\Github\Glancelot\...` doesn't exist in a cloud session, and cloud sessions are the likely authors of stacked PRs (B1's three parts are stacked). Suggest adding a portable form, such as `git show origin/main:scripts/ci/commit_lint.py > <scratch>/commit_lint.py`, then running that copy.
   - `<parent branch>..HEAD` would be clearer as `origin/<parent branch>..HEAD`.
7. **Date naming.** `docs/decisions/2026-11-06-asset-group-confirmations.md` and `integrate-pr18-2026-11-06/` carry November 6. The decision text, the commits (`2147010`, `30803be`, `398fb1a`, all 2026-11-05 -0700) and PR 18's `driver.log` (`11/05/2026`) say November 5. This is cosmetic, but a future reader matching dates against the November 6 commit-lint cutoff could be misled.
8. **Copy versus contract (cosmetic).**
   - The contract quotes "It's already in this folder." B1 also shows the plural "These items are already in this folder." for several items.
   - B1's "Checking the selected items…" is the contract's "Checking…".
   - Both are within the decision's intent.

### Observations (no action needed)

- The evidence logs don't carry the gated SHA themselves. `sha.txt` is the only link from the logs to the commit, as in earlier evidence folders. The SHAs do agree with the merges (see check 5).
- `lead_note_b1` is a new key with no earlier precedent in the queue. `pr_guard` accepts it.

## Checks

### 1. AGENTS.md: commands handled in the owning component (EN-07)

`ui/`, `tests/` and `src/` are identical between `origin/main` and `398fb1a`, so the checks against `origin/main` hold for the head too:

```text
$ git diff --quiet origin/main 398fb1a -- ui tests src && echo "ui/tests/src identical to origin/main"
ui/tests/src identical to origin/main
```

`export-selection` is handled on the Loader in Main. The `Connections` sits inside the `exportFlow` Loader, which is created with `Main.qml`. The handler that ends the loupe is declared last:

```text
ui/Main.qml:2183:    Loader { // B3 (F070): the export dialog and its worker load on first use, by URL, so startup never pays for them
ui/Main.qml:2199:        Connections { // here, not in ExportDialog.qml: the dialog does not exist until this first request
ui/Main.qml:2201:            function onViewCommand(id) { if (id === "export-selection") exportFlow.request() }
ui/Main.qml:2242:    // Declared last on purpose: QML connects a file's Connections in reverse declaration order (measured on Qt 6.8.3), so the
ui/Main.qml:2244:    Connections {
ui/Main.qml:2246:        function onViewCommand(id) { app.handleViewCommand(id) }
```

The owning components each have their own handler:

```text
ui/DeleteFlow.qml:118:        function onViewCommand(id) {
ui/FolderTree.qml:1038:        function onViewCommand(command) {
ui/PhotoGrid.qml:1085:        function onViewCommand(id) {
ui/RenameFlow.qml:34:        function onViewCommand(id) {
ui/TransferHud.qml:18:        function onViewCommand(id) {
```

The test the rule names exists and has an `exportFlow` row:

```text
tests/command_handlers_ui_tests.cpp:405:    void loupeEndsBeforeAMovedHandlerActs_data() {
tests/command_handlers_ui_tests.cpp:414:        QTest::newRow("exportFlow") << QString("export-selection") << QByteArray("exportFlow") << QByteArray(SIGNAL(activeChanged()));
tests/command_handlers_ui_tests.cpp:416:    void loupeEndsBeforeAMovedHandlerActs() {
```

`invoke` (line 478) and scope routing (`scopeOf`, `scopeAllows`, lines 491-539) are in Main.

The rule doesn't conflict with any other AGENTS.md section:
- *Hot files* lets only the granted packet edit `ui/Main.qml`, and everyone else adds a component with a thin call site. The new rule puts a command in its component, and puts only a lazily loaded owner's handler in Main. That handler is a call site in a hot file, so the lead owns it or grants it.
- *Command registry* blocks are untouched.

Two non-blocking points: the rationale (finding 1) and the wording (finding 2).

### 2. AGENTS.md: the batch line

The line agrees with `docs/RELEASING.md` and `scripts/ci/pr_guard.py`:

```text
scripts/ci/pr_guard.py:49: def is_integration_branch(head_ref):
scripts/ci/pr_guard.py:50:     return head_ref.startswith("lead/integrate-")
scripts/ci/pr_guard.py:147: def commit_errors(merge_base, head, number, head_ref):
scripts/ci/pr_guard.py:150:     if is_legacy_pr(number) or is_integration_branch(head_ref):
scripts/ci/pr_guard.py:151:         return []
```

- RELEASING.md says "The lead's `lead/integrate-<n>` batches carry those commits unchanged, so their commits aren't checked either; the lead batches only PRs whose own `guard` passed".
- AGENTS.md adds that the lead's own commits on a batch branch use the format, which is the gap that skip leaves open. Consistent.
- Merge commits are exempt anyway, and a batch's PR title is still checked (`title_errors` doesn't look at `head_ref`).

### 3. ASSET-GROUP "Lead decisions"

I read the whole contract. Nothing contradicts the new block:
- "No implicit companion inclusion anywhere, ever" and "file-only is the default every time" hold, because the straight-through path needs no companions at all and stays file-only.
- "Every companion is listed by name in the confirmation before anything happens" holds, because any companion forces the confirmation.
- "The Overwrite (with B2) … applies only to members that collide" is explicitly kept ("B2's Overwrite keeps its rule below"). The tension with it is finding 5.
- The only friction is wording (findings 3 and 4).

B1 (PR 16, `origin/claude/b1-asset-ui` at `0324e94`) implements the block.

*Straight through* (`ui/TransferFlow.qml`):

```text
    function nothingToDecide(plan) {
        return plan.targets.length > 0 && plan.rejected.length === 0 && plan.collisions.length === 0
            && plan.companions.length === 0 && !plan.companionsNotListed && !plan.companionsIncomplete && !plan.error
    }
```

*400 ms and Checking* (Cancel holds focus while it can't confirm):

```text
    property int progressDelay: 400
    Label { text: qsTr("Checking the selected items…") ...
    onOpened: (flow.canConfirm ? confirmAction : cancelAction).forceActiveFocus(Qt.TabFocusReason)
```

*Already in this folder*: a non-modal notice, and the cut stays cut.

```text
            if (first && flow.alreadyHere(plan)) {
                flow.noticeRequested(plan.rejected.length === 1 ? qsTr("It's already in this folder.") : qsTr("These items are already in this folder."))
                flow.discardPlan() // what was cut stays cut, for pasting somewhere else
```

*Whole-asset collision.* `src/app/copy_move_service.cpp` on B1 groups each asset by `splitAssetName(name, sidecar).stem`. If any member is occupied, it finds one shared `n` that is free for every member, and gives every member `stem (n)suffix`:

```text
        for (const auto& member : asset) collides = occupied(member.name) || collides;
        ...
            return QStringLiteral("%1 (%2)%3").arg(parts.stem).arg(n).arg(parts.suffix);
        ...
            for (int n = 2; n < 10000 && !shared; ++n) {
                const bool free = std::none_of(asset.begin(), asset.end(), [&](const Candidate& member) { return occupied(renamed(member, n)); });
```

B1's handoff (decisions 2, 15, 20 and 21) records the same three answers as "Lead's answer, November 5". It leaves no open questions for the lead.

The B2 note describes the tension correctly: B2's Overwrite replaces only colliding members, and B1 moves or skips whole assets. Finding 5 names the concrete hazard.

### 4. The RELEASING.md command

Run from PowerShell in the detached review worktree. The bad-subject probe is an unreferenced commit object, made with `git commit-tree` and dated after the cutoff. Nothing references or pushes it.

```text
PS> python C:\src\Github\Glancelot\scripts\ci\commit_lint.py --range origin/main..HEAD
exit=0
PS> python C:\src\Github\Glancelot\scripts\ci\commit_lint.py --range origin/main..origin/claude/b1-asset-ui
exit=0
PS> $probe = git commit-tree "398fb1a^{tree}" -p 398fb1a -m "Bad subject for a lint probe"   # dated 2026-11-07
probe=acbabda6ab7a3099ca5babaf94565e98be189516
PS> python C:\src\Github\Glancelot\scripts\ci\commit_lint.py --range "origin/main..$probe"
commit_lint: commit acbabda6a: 'Bad subject for a lint probe': use 'type(scope): description', for example 'fix(grid): keep the selection during arrival' (docs/RELEASING.md)
exit=1
```

- The main checkout is on `main`, and its `scripts/ci/commit_lint.py` was last changed at `1395a4d` (PR 19), so it is the current copy.
- B1's range passes because all its commits predate the November 6 cutoff.
- A first attempt with `git commit` was refused by the installed `commit-msg` hook, which confirms the hook is active.
- Portability is finding 6.

### 5. Evidence READMEs against their logs

```text
== integrate-pr18-2026-11-06
integrate-pr18-2026-11-06/windows.log:994:100% tests passed out of 55
integrate-pr18-2026-11-06/docker.log:1586:#25 699.3 100% tests passed, 0 tests failed out of 54
#37 naming to docker.io/library/glancelot-gate-pr18:latest done
== integrate-pr19-2026-11-05
integrate-pr19-2026-11-05/windows.log:962:100% tests passed out of 54
integrate-pr19-2026-11-05/docker.log:1534:#25 627.8 100% tests passed, 0 tests failed out of 53
#37 naming to docker.io/library/glancelot-gate-pr19:latest done
```

The `docker-image-ls.txt` files show `548MB` content size for both. The `summary.txt` and `driver.log` files show `WINDOWS exit 0` and `DOCKER exit 0` for both. Neither `windows.log` mentions a rerun; the only "Failed" line is CMake's `CMAKE_HAVE_LIBC_PTHREAD` probe.

The SHAs match the merges, and each merge's tree equals the gated commit's:

```text
$ git log -1 --format='%h %P %s' 30803be
30803be d8c01a73640c077eff12f6184db868d3607c9f64 2147010c44f470209769ef9287326ea3d315bb6e Merge pull request #18 from dvortsis/claude/en-07
$ git diff --quiet 2147010 30803be && echo "30803be tree == 2147010"
30803be tree == 2147010
$ git log -1 --format='%h %P %s' d8c01a7
d8c01a7 20f12103cc6427ac0214f713476b1c1c88bbbbef 1395a4d405f9de52f763b5efa5b57ff7f563f3c7 Merge pull request #19 from dvortsis/lead/conventional-commits
$ git diff --quiet 1395a4d d8c01a7 && echo "d8c01a7 tree == 1395a4d"
d8c01a7 tree == 1395a4d
```

PR 18's README says the gated head "adds only a conflict-free merge of `main`" to the reviewed `d228f43`. The parents and the remerge diff agree. An empty remerge diff means nothing was resolved by hand, so the lead posting `fresh-review` there fits AGENTS.md:

```text
$ git log -1 --format='%P' 2147010
d228f431a0686b5088662d58ee85696ae15fa769 d8c01a73640c077eff12f6184db868d3607c9f64
$ git show --remerge-diff --format= 2147010 | wc -l
0
```

The reviews the READMEs cite agree:
- `EN-07-REVIEW.md` is **APPROVED** at `d228f431a0686b5088662d58ee85696ae15fa769`.
- `PR19-CONVENTIONAL-COMMITS-REVIEW-R4.md` is **APPROVED** at `1395a4d405f9de52f763b5efa5b57ff7f563f3c7`.

### 6. Queue and guard

```text
PS> python scripts/ci/pr_guard.py --base origin/main --head-ref lead/closeout-2026-11-06 --head 398fb1a
pr_guard: code=False, 0 error(s)
exit=0
PS> python -c "import yaml ...; print(len(d['tasks']), t['EN-07']['state'], t['EN-07'].get('integrated_commit'), [k for k in t['B2']])"
91 done 30803be ['id', 'title', 'owner', 'state', 'depends', 'lead_note_b1', 'brief_notes']
exit=0
```

The PR's own checks on `398fb1a` are `guard` SUCCESS and `ci-ok` SUCCESS, with `core` and `viewer` SKIPPED (docs only). The PR title passes the format. As lead bookkeeping on a `lead/` branch that touches only the queue, contract text, a decision, AGENTS.md, RELEASING.md and evidence, it needs `ci-ok` plus this review.

### 7. Copied review documents

Each file was compared, with CR stripped, against its source branch:

```text
A1-REVIEW-PR12.md: origin/claude/review-a1-pr12(MATCH)
A1-REVIEW-R2-scratch-tests.patch: origin/claude/review-a1(MATCH)
A1-REVIEW-R2-trial-fix.patch: origin/claude/review-a1(MATCH)
A1-REVIEW-R2.md: origin/claude/review-a1(MATCH)
EN-07-REVIEW.md: origin/claude/review-en-07(MATCH)
PR19-CONVENTIONAL-COMMITS-REVIEW-R2.md: origin/claude/review-pr19(MATCH)
PR19-CONVENTIONAL-COMMITS-REVIEW-R3.md: origin/claude/review-pr19(MATCH)
PR19-CONVENTIONAL-COMMITS-REVIEW-R4.md: origin/claude/review-pr19(MATCH)
PR19-CONVENTIONAL-COMMITS-REVIEW.md: origin/claude/review-pr19(MATCH)
```

The two A1 reviews are recorded as CHANGES REQUIRED, which is correct as history. This PR doesn't change A1's queue state.
