# STACK-F015-RECOVERY review, round 3

**Verdict: APPROVED**

Round 3 closes B2. The B2 fix, the unavailable state, the Retry route and the placeholder all check out, the shared header is untouched, and every targeted suite passes on Windows. There are no blocking findings. Three non-blocking notes (N1 to N3) are recorded for follow-up.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code review subagent. It did not implement this packet and did not do the earlier rounds. |
| Candidate | `origin/claude/stack-f015-recovery` at `b498e09` |
| Round-3 delta reviewed | `git diff 97fb2ff b498e09` (product: `src/app/viewer_controller.cpp`, `ui/Main.qml`, `cmake/TabsRecoveryTests.cmake`, `cmake/ReferencePinTests.cmake`; tests: `tests/tabs_recovery_tests.cpp`; the origin/main merge `97fb2ff` excluded) |
| Prior review | `origin/claude/review-stack-f015-r2` `docs/verification/STACK-F015-RECOVERY-REVIEW-R2.md` (CHANGES REQUIRED, B2) |
| Environment | Windows 11, MSVC 2022, Qt 6.8.3 (`.tools`), Ninja Release, offscreen. Build `build-review-f015-r3`, worktree `.worktrees/review-f015-r3`. |

## B2: closed

**What the fix does** (`src/app/viewer_controller.cpp`):

- `settleOn()` records `settleFolder`. `notePosition()` settles only when the image was opened after the switch (the round-2 `openSerial` fence) and `settlesIn(folder)` holds, that is, the image sits in the target tab's own folder or the user has taken the tab over (`settleAny`) (about line 487).
- The no-images folder-visit branch settles only when `settlesIn(visited)` (about line 1084).
- `AutomaticMove` scopes wrap `reconcileObservation()`, `reconcileRenamedPath()` and the tab route (`goToTarget()`). `noteUserMove()` runs at `open()` (when not from navigation), `navigateDirectory()`, `next/previous/nextFolder/previousFolder`, and only counts outside an automatic scope.
- When the switch's own folder route fails or is cancelled, `showUnavailable()` retires the previous tab's content (generation bump, cancel, path, image, metadata, watch scope). Nothing of the previous tab is left for F5, a watcher event, a rename or a delete to reopen. This removes the cause behind all four R2 probe rows and also N4 from R2 (stale image under the target tab).
- The pending position stays the target's stored location, so the checkpoint and the destructor's `clean` record the right tab set.

**Tests, and the spot-check that they fail without the fix.**

- New tests in `tests/tabs_recovery_tests.cpp`: `reopenAfterAFailedFolderSwitchKeepsTheTarget` (F5, external edit, external delete, F5 after stale decode: the four R2 probe rows), `automaticMoveDuringAPendingSwitchNeverSettlesIt` (rename, removal), `cancellingTheSwitchFolderRouteLeavesTheTabUnavailable`, `retryReopensTheTargetWhenItsFolderReturns`, `retryWhileStillUnavailableKeepsEveryTabsLocation`. The preconditions are asserted (the switch really failed; the file really changed or was removed).
- **Spot-check (mutation, this review).** I replaced the body of `settlesIn()` with `return true` (the folder rule reverted), rebuilt `tabs_recovery_tests`, and ran it. Result: exactly one failure, matching the handoff's M6 row.

  ```
  FAIL!  : TabsRecoveryTests::automaticMoveDuringAPendingSwitchNeverSettlesIt(removal followed) 'live.isEmpty()' returned FALSE.
           (live tab 1 holds the wrong location: live tabs [.../alpha/a.png, , .../gamma/c.png] active 1)
  Totals: 67 passed, 1 failed, 0 skipped, 0 blacklisted, 59189ms
  ```

  The source was then restored (`git checkout`), the target rebuilt, and the clean run passed (below). The handoff's other mutation rows (M5, M7, M8) and the fail-before output for the merged tree were not re-run here; they read as consistent with the code.
- The two tests changed on purpose (`staleDecodeCannotSettleADirectorySwitch`, the folder-route rows of `secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation`) keep every location assertion and now assert the unavailable state instead of the stale photo. This matches lead decision (b).

## Lead decision (b), the unavailable state, and Retry

- `goToTarget()` records the navigation sequence it submits in `settleNavigation`. `switchFailed` requires `sequence == settleNavigation`, so only the switch's own route triggers `showUnavailable()`, and an unrelated failed navigation does not.
- `cancelDirectoryNavigation()` handles the hanging-share case (Esc) the same way and returns before the stale-path rescan. Tested.
- `switchSuperseded` handles an automatic navigation that replaces the switch's route: the route is run again after that result. The handoff's M7 mutation covers it.
- **Retry routes to the existing command.** `ui/Main.qml:1319` (`onClicked: app.invoke(emptyState.unavailable ? "refresh" : "open")`) goes through `app.invoke` to `viewer.trigger("refresh")`. `trigger()` runs `retryTarget()` when `tabUnavailable` (about line 1586) and the old `observe()/refresh()` otherwise. So the button and F5 are the same command. The tooltip appends `app.shortcutHint("refresh")`, and `refresh` is `F5` in `command_registry.cpp`. `retryTarget()` calls `settleOn()` again, which clears `tabUnavailable`, so a successful retry settles normally and a failed one returns to the unavailable state. Tested both ways.
- **No header change.** `git diff --stat 97fb2ff b498e09 -- include` is empty. `tabs()` gains an `unavailable` key in the existing `QVariantList` of maps, which needs no header change.

## The placeholder (`ui/Main.qml:1226-1335`)

- It is a variant of the existing `emptyState` (the canonical empty-state primitive), not a new component. The mascot, brand heading and tagline hide; a muted `UiIcon` folder glyph, a heading "<folder> is unavailable", the middle-elided native path, one primary Retry button and a one-line hint show. This is the right shape per application-interface-design ("explain the situation and offer one relevant next step").
- **Strings.** All new user-visible QML strings use `qsTr` (`"%1 is unavailable"`, `"Retry"`, `"Retry this tab's folder"`, the hint line). The status text in C++ goes through `QCoreApplication::translate("Status", ...)`. The pre-existing `Accessible.name: "Open a photo"` for the non-unavailable branch was already not translated, and is not new.
- **Tokens and tokens exist.** `Theme.textHeading`, `Theme.gapXl`, `Theme.textSmall`, `Theme.muted` and `Theme.text` all exist in `ui/Theme.qml`. No new colours, radii or motion.
- **Logic.** `unavailableTab` reads `rows.length > 1 ? rows[viewer.activeTab] : undefined` and binds to `viewer.tabs`, whose `tabsChanged` is emitted by `showUnavailable()`, `settleOn()` paths and `retryTarget()`. The empty state's `visible` still requires `fileCount === 0 && !loading`; `showUnavailable()` empties `nav`, so it holds. Compact panes keep the title and Retry and hide the icon, path and hint, as the welcome variant does. The accessible role (`AlertMessage`) and name are set, and the Retry button is in the Tab order.
- **Skills.** The ui-design-skills are available at `~/.agents/skills/ui-design-skills`. I read application-interface-design's state and empty-state guidance and judged the placeholder against it; I did not read the other skills in full.
- **Not rendered.** I did not render the placeholder. No existing harness drives the active tab into the unavailable state and grabs the `emptyState` item, and building one would be new test code outside this review's no-product-change rule. Evidence instead: `qmlcachegen` compiled `Main.qml` in the app build, and `reference_pin` and `translation_coverage_ui`, which load the shipped `Main.qml` offscreen, pass (below). The visual result (spacing, wrapping of long folder names, 48 px glyph) is therefore **unverified** and is best checked by the user or by a follow-up grab test.

## Known gap: one remaining tab with an unavailable folder

`tabs()` returns no rows when fewer than two tabs exist (`viewer_controller.cpp:1524`), so the placeholder's `rows.length > 1` test is false and the welcome variant shows. Two routes reach it: closing the active tab so that one unavailable tab remains, and closing the other tab while the active one is unavailable (reasoned from the same code; the handoff names only the first).

**Judgement: acceptable as a recorded follow-up (non-blocking).**

- Data is safe: the pending position is the stored location, the previous tab's content is retired, the record is checkpointed correctly, and the status line names the folder. F5 retries, and opening a photo takes the tab over.
- The user sees the welcome screen ("Open a photo") instead of "folder X is unavailable". This is misleading but not harmful, and it is the same experience as a restore whose folder is gone.
- A fix without a header change exists: let `tabs()` return the one active row when `tabUnavailable` is set, and keep the `> 1` checks in the tab strip (`visible: viewer.tabs.length > 1`) and the placeholder read the row with `length >= 1 && row.unavailable`. This needs a test and a check that `window_slots` is unaffected (it relies on an empty list at one tab, which would still hold when nothing is unavailable). The lead should queue it.

## Non-blocking notes

- **N1 (Low): cursor-preview `open()` counts as the user's move.** `open()` calls `noteUserMove()` whenever the call is not from a navigation result. The combined-mode cursor preview (`previewDebounce` -> `open()`, `ui/Main.qml` about line 146) goes through the same path. If a relist moved the grid cursor while a switch's folder route is still pending and the previous tab's listing is still shown, the preview could set `settleAny` and let the previous tab's file settle the target. This is narrow (combined mode only, inside the pending window; after `showUnavailable()` there is nothing to preview) and was not reproduced. A future guard would wrap the preview open in an `AutomaticMove`-style flag. The R2 review raised the same trigger, so I record it rather than block on it.
- **N2 (Info): translation catalog.** The new strings are not in `translations/glancelot_en_XA.ts`. The handoff says so; `translation_coverage_ui` passes because the elements are hidden on the default surfaces. The catalog owner should regenerate.
- **N3 (Info): grid and combined mode.** `emptyState` is hidden there, so the unavailable message shows only in the status line. F5 still retries. The handoff records this.

## Grant check

`git diff --name-only 97fb2ff b498e09`: the two cmake files (lead's one-time grant, plus `${GLANCELOT_VIEWER_CONTEXT_SOURCES}` in `ReferencePinTests.cmake`, justified in the handoff by the `FavoritesModel`/`FolderHistory` link errors), `src/app/viewer_controller.cpp`, `ui/Main.qml`, `tests/tabs_recovery_tests.cpp`, and the handoff. Nothing under `include/`. The cmake change links on the merged tree (both targets built).

## Test output (this review)

Build: `build-slot.ps1 -Branch review-f015-r3 -Run { cmake --build build-review-f015-r3 }` (full build, exit 0).

```
ctest -R "tabs_recovery|recovery|folder_tree|viewer_acceptance"   (inside gate-lock.ps1)
1/7 Test  #7: viewer_acceptance ................   Passed   11.99 sec
2/7 Test  #9: folder_tree ......................   Passed   10.75 sec
3/7 Test #38: recovery .........................   Passed   14.52 sec
4/7 Test #39: recovery_startup .................   Passed    0.55 sec
5/7 Test #40: recovery_controller ..............   Passed   15.41 sec
6/7 Test #41: recovery_process .................   Passed   43.88 sec
7/7 Test #42: tabs_recovery ....................   Passed   64.72 sec
100% tests passed out of 7
```

After the mutation check was reverted and rebuilt:

```
1/3 Test #42: tabs_recovery ....................   Passed   60.08 sec
2/3 Test #45: translation_coverage_ui ..........   Passed   24.65 sec
3/3 Test #46: reference_pin ....................   Passed   25.61 sec
100% tests passed out of 3
tabs_recovery Totals: 68 passed, 0 failed, 0 skipped, 0 blacklisted, 59960ms
```

The machine was shared with other reviewers' builds, so the run times are higher than usual; none of the suites in this set is a budget test except `recovery_controller`, which passed on the first run.

## Unverified

- The placeholder was not rendered or screenshot (see above).
- The full offscreen suite was not run; the authoritative gate on the integrated commit decides.
- Handoff mutations M5, M7 and M8 and the fail-before output were not re-run here.
- Physical, multi-window and GPU paths were not exercised.
- N1 was reasoned from code, not reproduced.
