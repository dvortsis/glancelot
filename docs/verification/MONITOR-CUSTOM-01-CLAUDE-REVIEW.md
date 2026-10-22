# Lead integration closeout — October22,2026

MONITOR-CUSTOM-01 is integrated at008d026951428988e545c63c25ded0b6106e3882. Source/test approval below is retained verbatim; its pending-gate statements describe the review checkpoint. The lead subsequently recorded full Windows41/41, fresh Linux Docker41/41, exact-merge focused5/5, complete native14/14, native two-display frame/move proof and isolated25-frame production rendering. [Final handoff and actual command output](../handoff/MONITOR-CUSTOM-01-CODEX-HANDOFF.md). Physical mixed-DPI/refresh/color/HDR qualification remains MONITOR-VERIFY. Queue done; serial integration lock released after these checks.

---

I've finished reading the delta, the native failure traces and the CMake registration. I'm writing up the verdict now.

# MONITOR-CUSTOM-01 at a0b00ff: APPROVE SOURCE / FULL GATES PENDING

I found no blocking test-delta issue. I read only; I ran nothing and edited nothing. I took "product code is Git-identical to 70a6903" from `delta.patch`, which touches only `tests/monitor_customization_ui_tests.cpp`. I did not check it with Git. Native run 12 is unverified. The offscreen 31/13 results and the full `folder_grid` pass come from your report and the archive, and I did not re-check them.

## Fixture validity

**Native run 11 shows two failures, not one.** Evidence is `monitor-native-ui-attempt-11.txt` lines 8 and 13.
- `nativeUiEvidence` failed `h.window->isExposed()` before any click.
- `settingsWorkspaceEntryOpensMonitorDialog` failed on the monitor dialog not opening.

The first failure points at the native window not being reliably exposed or raised at that point. That fits the second one: a window that isn't exposed or active may not have a settled layout.

**The signal trace fits a stale click point.**
- `native-entry-signal-extract.txt` lines 39-41 show `openMonitorSettings` getting `yChanged`, `widthChanged` and `availableWidthChanged` after Settings reports `opened`.
- Nothing follows that, and no `pressed` or `clicked` signal appears anywhere.
- Settings only closes at teardown (line 48).
- So the click did not land on the button. A point computed before a late layout change explains this. So does a window that was never really exposed or active.

**The cause is inferred, not proven.** The trace shows what did not happen, not why. Run 12 is what tests the explanation. The delta covers both candidate causes, so it is a reasonable fix either way.

## Does it strengthen the native proof?

Yes. Nothing is loosened or bypassed.

- **`raise()` before `requestActivate()` (`:95`).** The existing `isExposed` timeout of 5000 ms and the `frameSwapped` requirement are unchanged. This only makes the existing requirement more likely to be met.
- **The entry test now requires `isExposed()` and `isActive()` before interacting (`:140`).** Before, it had no such precondition. A window that never became exposed would now fail at that line with a clear message. Before, it failed later with a misleading "dialog did not open". This is a stricter precondition, not a weaker one.
- **The click point is computed after the screenshot (`:144-145`).** The screenshot still saves first, so the evidence PNG is captured before any point-related assertion. `mapToScene` and the in-window `QVERIFY` run right before `QTest::mouseClick`. The click is still a real `QTest::mouseClick` at the real scene point. The assertions that the monitor dialog opens and Settings closes are unchanged.
- **Gate impact.** `cmake/MonitorCustomization.cmake:57-59` runs both suites with `QT_QPA_PLATFORM=offscreen`. The new `isActive()` wait therefore applies to the offscreen gate as well. Your focused offscreen run passes. `recoveryKeyPointerAndPaletteShareLockedRouting` already waits on `host->isActive()` (`:117`), so the pattern is established. I see no new gate hazard.

## Races

- **None introduced.** Between `mapToScene` and `mouseClick` there is no event-loop turn. `QTest::mouseClick` on a window delivers synchronously. Moving the computation after the grab narrows the stale-position window, and does not widen it.
- **One residual risk, non-blocking.** The `qWait(100)` and `QTRY_VERIFY(action->isVisible())` pair is still a fixed delay. It is not a stability check on the button's geometry. If layout can still change after the wait, the click point could still be stale. A stronger fixture would use `QTRY` to wait until the button's scene point is unchanged across two consecutive polls, or until the dialog's open transition has finished. Run 12 will show whether this matters.
- **The screenshot is a fresh synchronous render.** It does not capture a pre-layout frame, so it is a valid record of the layout at that moment.
- **The native-only grab ordering has no effect in the normal gate.** `grabWindow` is only called when `GLANCELOT_MONITOR_UI_EVIDENCE` is set. The reorder cannot change what the normal gate exercises.

## Non-blocking suggestions

1. **Check the hit target before clicking.** Check that `action->contains(action->mapFromScene(point))` holds, or that the click target really is the button. On a miss, the assertion fails with a clearer message than a closed dialog.
2. **Record foreground state in the native proof.** `run-monitor-native-proof.ps1` could log whether the session was foreground and unlocked. If Windows refuses `requestActivate()` because the process isn't foreground, `QTRY_VERIFY(isActive())` will time out. That would be an environment fault and not a product regression.
3. **Keep the exposure and activation waits.** If run 12 passes only after they are added, record that in the evidence.

## Before integration

- Native run 12 on this immutable candidate, with the binary hash provenance. I haven't seen it, and I'm not assuming it passes. If it fails again, the failing line and its signal trace are the discriminator between a stale click point and an environment fault.
- Complete `folder_grid` on this exact commit, since the test file changed.
- The locked Windows gate and `docker build` on the exact integrated commit.
- The carried-forward 70a6903 items stand. Those are the search-reach gap for the new Browsing button, the maximize-then-move native case, and the windowed output frame-fit check.

No new product concern. Approved as a test-only delta; gates pending.


Lead provenance: actual Sonnet5.5 session f6bcf65f-83fb-4740-a4c9-4adf8f8801fd;73086ms;exit0;is_errorfalse;no permission denials. Product code equals approved70a6903; this approval covers stricter test setup only. Subsequent exact-candidate native12 passed14/14 in56738ms with identical source/binary hashes. Full gate retry and integrated native proof pending. Earlier source reviews and all failed/native attempts remain in packet archive. These notes do not alter the actual reviewer verdict above.
