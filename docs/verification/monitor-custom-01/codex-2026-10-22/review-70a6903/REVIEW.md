I've finished reading the delta and its context. Next I'm writing up the verdict.

# MONITOR-CUSTOM-01 at 70a6903: APPROVE SOURCE / FULL GATES PENDING

I found no blocking delta issue. I read the delta patch, `ui/SettingsDialog.qml`, the new test, the `folder_grid` CMake line, `Main.qml` command handling and the two prior reviews. I ran nothing and edited nothing. This is not an integration approval. The Windows and Docker gates and the native proof are unverified, and so is your claim that `paletteKeyboardCursorStaysInViewport` now passes 3/3 (I took it from your report).

## Delta assessment

| Item | Verdict | Evidence |
|---|---|---|
| Compact scroll fix | **Correct, and the right kind of fix.** | The button was removed from the shared `sidebarCol` (`SettingsDialog.qml:164-166`). That column now holds only search and nav, so every section's content viewport gets its row back, not just Appearance. The legacy test is untouched and nothing was relaxed. Nothing in `tests/` depends on the old sidebar focus order (no `settingsSearch` or focus-chain references). |
| New home | **Semantically right.** | The button sits under "Workspace layout" on the Browsing page (`:362-370`). It reuses the existing `dialog.close(); host.invoke("monitor-settings")` path, which `Main.qml:618` handles. The `monitor-settings` command in `command_registry.cpp:71` and the `DetachedPaneHost.qml:64` button are unchanged. |
| Accessibility | **Fine.** | `UiButton` is `StrongFocus` with `Accessible.name: text` (`UiButton.qml:34,39`). It is the first focusable control on the Browsing page, so the tab order is logical. The "…" suffix signals a dialog. The label is visible text, not icon-only. |
| New test | **Meaningful.** | It covers 540×360, 720×540 at 150% text, and 1180×780. It opens Settings to Browsing and requires the button to be visible. It then clicks the real scene point with `QTest::mouseClick` and requires the monitor dialog to open and Settings to close. The click is the strong part. A button clipped by the `ScrollView` (`clip: true`) would not receive it, and the monitor dialog would not open. It sits in the per-feature file as AGENTS.md requires, and the headers it needs (`QDir`, `setTextScale`) are present. |
| Logger syntax | **Valid.** | `-o "<bindir>/folder_grid-results.txt,txt" -o -,txt` uses QtTest's repeatable `-o file,format`. Qt splits on the last comma, so the drive-letter colon is harmless. CMake quotes the path. The `TIMEOUT 400` and environment lines are unchanged. The stdout logger is kept, so existing console output is unchanged. |

## Non-blocking observations

1. **Search no longer reaches the button.** The Browsing clause of `sectionMatches` (`:67-74`) does not list "Displays and workspaces". The old button was always visible, so this is a small new discoverability gap. Typing "display" or "monitor" in Settings search will not switch to Browsing. A one-line addition fixes it: `|| rowMatches("Displays and workspaces", "Monitors, second display, presentation output")`. The command is still reachable through Controls and the palette.
2. **Inconsistent button width.** `Layout.fillWidth: true` (`:368`) makes this button full width, but the "Arrange panes" button below it is intrinsic width. Other action buttons on the Settings pages are also intrinsic. Dropping `fillWidth` would match the surrounding rhythm. Slate/Ice/Geist tokens, spacing and typography are otherwise untouched.
3. **Weak assertion in the test.** The explicit assertion only checks that the point is inside the window (`QRectF(QPointF(), window->size())`). Clipping is caught only through the click outcome. Asserting that the point lies inside the Settings page or dialog bounds would make the failure message clearer.
4. **No keyboard test.** There is no keyboard-activation test for the new entry. The control is a standard `UiButton`, so I'm not blocking on it.
5. **Silent no-op without a monitor object.** If `app.monitor` is null (the `grid_ui_tests` harness), the button closes Settings and opens nothing. This is pre-existing behaviour, not a regression.

## Carried forward from the earlier reviews

- The 6e22804 and af339f9 pre-integration items stand. These are one clean native run on the integrated commit, a windowed output frame-fit check, and the maximize-then-move case.
- The unchanged minors also stand. Emergency recovery doesn't survive a restart. Bounded retries are counter-only. `computeDisplayId` is O(n²). The `qInfo` prints and the repeated accessible names remain.

## Before integration

- Get a complete `folder_grid` pass on this source. Your current rerun is the evidence I'm waiting for. I can't confirm the 258.70 s failure is gone from reading alone.
- Run the locked Windows gate and `docker build` on the exact integrated commit.
- Do one native run, as listed above.

The correction is a small, well-scoped layout change with a test that exercises the real entry path. Source approved; gates pending.
