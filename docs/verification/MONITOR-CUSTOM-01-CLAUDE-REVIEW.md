# MONITOR-CUSTOM-01 at af339f9: APPROVE SOURCE / FULL GATES PENDING

I found no blocking delta bug. I read the delta patch and the source around it, and ran nothing. This is not an integration approval, and the Windows/Docker gates and the native run are unverified. The review is read-only and was not saved to a plan file.

## Delta assessment

| Delta item | Verdict | Evidence |
|---|---|---|
| Windowed output keeps its maximized/minimized state | **Correct for option and color changes.** | `presentation_window.cpp:123` calls `showNormal()` only on a transition into windowed. A new window has an empty `previous`, so first show still works. `monitor_customization.cpp:124` rewrites geometry only when the call is initial or `visibility()==Windowed`. `initialWindowed` (`:103`) no longer fires on `visibility!=Windowed`, but still fires on a mode change or a frameless window. A fullscreen-to-windowed switch is initial and gets `showNormal` plus the 80% centred size. Theme changes go through `colorChanged` (`:213`) into the same path, so closing prior non-blocking item 3 is correct. |
| Native frame-margin test | **Right test, right numbers.** | `monitor_customization_tests.cpp:268` uses margins 8/31/8/8 on the work area (-1920,-1200,1920,1080). Initial size is 1920·4/5−16 = 1520 wide and 1080·4/5−39 = 825 high, matching `QCOMPARE(…,QSize(1520,825))` against `windowedClient` (`monitor_customization_topology.hpp:29`). The oversized preferred rectangle is clamped inside the work area. A reachable rectangle is returned unchanged. This closes pre-integration requirement 2 from review-6e22804 at helper level. A native frame is still unproven (see below). |
| `customization.saving` replaces feedback matching | **Correct.** | `pending=false` is set at `monitor_customization.cpp:181`, before `feedback.clear()` and `notify()` (`:186,189`). `finishEnable` therefore runs once the save finishes. During the save, `pending` is true and `notify()` at `:192` returns early. The `saving` property exists (`monitor_customization.hpp:22`). This closes the open `finishEnable` minor. |
| Connection-type text | **Honest and consistent.** | `DisplayInfo` and `display_manager.cpp` have no connection field, so "Connection type unavailable · Color: …" is accurate. The old hard-coded "Connected" is gone. The label keeps `Layout.fillWidth`, `Text.Wrap` and `Theme.muted`, so typography, spacing and theme tokens are unchanged, and the longer string wraps instead of clipping. No new controls or framework changes, so no design-review concerns. Connection type stays an unmet plan §1 detail, now stated honestly. |
| New maximized/minimized regression | **Catches the presentation-side regression.** | `monitor_customization_tests.cpp:234-237` maximizes, changes `guides`, and expects Maximized. It then minimizes, changes `imageInfo`, and expects Minimized. The old `visibility()!=Windowed → showNormal` would fail this. |

## Non-blocking concerns from this delta

1. **Moving the output while it is maximized or minimized is unproven natively.** The trigger is `moveOutput` (`:310`) or a `moved` display change, which reaches `openPresentation` (`display_manager.cpp:442-448`). That call runs `setScreen`, then `setGeometry(screen->geometry())`, then `show()` on the existing window.
   - Before af339f9, a non-Windowed state made `initialWindowed` true, so the window was reset to a clamped 80% size with `showNormal`. Now neither the geometry rewrite nor `showNormal` runs.
   - A maximized window may then stay maximized on the new display. A minimized window may later restore at the raw full-screen rectangle (frame included) instead of the work area.
   - It is user-initiated, recoverable and loses no data. If it misbehaves in the native run, the fix is small: treat `moved` with a Maximized or Minimized window as initial.
2. **The new test covers only the presentation half.** The offscreen QPA ignores `setGeometry` on a non-normal window, as far as I recall. I could not check Qt sources here. `:124` could therefore regress and the test would still pass. That coordinator condition is protected only by reading the code.
3. **No window opens with real frame margins.** The helper test proves the arithmetic on supplied margins. The native run should still open one windowed output and confirm it fits.

## Carried forward, unchanged

- The review-6e22804 notes still apply: emergency recovery does not survive a restart, and bounded retries are counter-only.
- The unchanged minors remain: the `computeDisplayId` O(n²) enumeration, `qInfo` prints in `nativeUiEvidence`, repeated accessible names, and the 720 px / 150% text layout.
- Hardware, mixed-DPI, colour and HDR behaviour, and a fullscreen controller swap on real multi-display hardware, remain unverified and belong to MONITOR-VERIFY.

## Before integration

- Run the locked Windows and Docker gates on the exact integrated commit.
- Make one clean native run with the planned binary hash and source equality.
- In that run, include one windowed output frame fit and the maximize-then-move case from concern 1.


Lead provenance: actual Claude Code Sonnet5.5 session fcc1ab9f-4a81-4dbc-b2d9-18b72e945d4a;120526ms;exit0;is_errorfalse;no permission denials. Base approval at6e22804 retained in packet archive. Subsequent candidate native run13/13 and real two-display frame/move probe pass; raw source/binary hashes and outputs retained in packet archive. Full locked gates and integrated native proof remain pending. These lead notes do not alter the actual reviewer verdict above.
