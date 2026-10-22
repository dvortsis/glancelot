# MONITOR-CUSTOM-01 Codex evidence, October22,2026

The exact source commit is recorded in the parent handoff and final independent review. This archive retains actual raw command output and generated native screenshots. No hardware color/HDR/refresh or physical mixed-DPI qualification is claimed.

Focused commands (own assigned worktree/build directory):

```powershell
& ./.cache/build-monitor-focused.ps1 -BuildDir build-codex-monitor-custom-01
# Later incremental attempts use the same MSVC/Qt bootstrap and this configured target command:
cmake --build build-codex-monitor-custom-01 --target monitor_customization_tests monitor_customization_ui_tests --parallel 4
ctest --test-dir build-codex-monitor-custom-01 -R '^monitor_customization(_ui)?$' --output-on-failure
# Native proof: Qt project-local bin on PATH, QT_QPA_PLATFORM=windows,
# QT_QUICK_BACKEND unset, QSG_RHI_BACKEND=d3d11; generated media only.
./build-codex-monitor-custom-01/monitor_customization_ui_tests.exe nativeUiEvidence nativeHostCloseAndEscapePreserveController -o .cache/monitor-native-ui-attempt-4.txt,txt
```

The full bootstrap script was copied from scripts/build.ps1 with only the target list narrowed; these targeted builds do not use -Test and are not full gates. Earlier failures are retained: Qt slots macro/compile fixes, a QML stackBefore call, ApplicationWindow content-item test correction, inactive-grid fixture, and an explicit comparison-open test correction. Native attempts1/2 were interrupted while apparently unresponsive; step instrumentation and cached topology/route getters resolved excessive Windows identity enumeration. Attempt3 passed rendering but failed a position assertion after native-window close; attempt4 observes position during closing and passes both proofs. Attempt5 runs the complete native UI suite:10 passed,0 failed,0 skipped.

Raw Qt results are retained because GUI executables did not populate redirected stdout. The lead CMake amendment now writes explicit Qt result files as well as CTest output. Final focused attempt9 passes both suites; its Qt logs and native attempt5 are the latest passing results. The native test is explicitly skipped in default offscreen gates and separately executed on Windows. Six generated-image detach/re-dock cycles retain decoded state and render after returning to the controller. Screenshots cover four pages, compact720px with150% text and wide1280px, in dark/light themes.

Full gate logs and final cross-lane review will be added before integration. Failed/interrupted attempts are not passing evidence.

Final correction evidence:26 coordinator tests pass;9 UI tests pass with the native-only evidence method skipped in offscreen mode; the separate complete Windows/D3D11 UI run passes all10 tests. Added a real rebound Ctrl+Alt+9 press and recovery-button pointer click while frozen/locked. This exposed transient-window duplicate shortcut ownership and cache invalidation occurring after QML notification handlers. Both were corrected, with default/window-local context retained and the controller key-table copy disabled while a detached host owns focus. Cached route/topology data now invalidates explicitly before notifying readers. Pinned output ignores mirror follow/folder/timing policy and a regression checks unpinning resumes from the current folder.

The actual passing focused attempt9 summary is:

```text
1/2 Test #40: monitor_customization ............   Passed    8.22 sec
2/2 Test #41: monitor_customization_ui .........   Passed   17.32 sec
100% tests passed out of 2
Total Test time (real) =  25.66 sec
```

The latest native command ran the entire executable (no test-name filter), with GLANCELOT_MONITOR_UI_EVIDENCE pointing at a disposable evidence directory:

```text
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 37544ms
```

## Corrections after independent review at2f73c84

The actual Sonnet5.5 review returned CHANGES REQUIRED; its complete prompt, pinned diff, result and findings are retained in review-2f73. Historical screenshots it reviewed are retained in native-ui-reviewed-2f73. The latest native-ui screenshots are correction attempt8.

Corrected canonical freeze ownership (layouts[slot] only), layout-only invalid/newer-state transactions, pending-save geometry rollback, native output windowed frame bounds and live screen bookkeeping, explicit cross-display drag preference, black output default, atomic viewport validation, and labeled/focused destination/preset controls with full-width switches. Real coordinator QPA tests now exercise one/two/three displays, positive move/swap, pin/transform retention, strong/weak hotplug and mixed origins/DPR. Real native grid tests preserve focus, scroll, content mode and selection. Workspace preset guards include all seven canonical panes.

Final focused command: ctest --test-dir build-codex-monitor-custom-01 -R '^monitor_customization(_ui)?$' --output-on-failure (MSVC/project Qt bootstrap). Actual attempt15 output:

```text
1/2 Test #40: monitor_customization ............   Passed    9.00 sec
2/2 Test #41: monitor_customization_ui .........   Passed   26.62 sec
100% tests passed out of 2
Total Test time (real) =  35.78 sec
```

Actual Qt totals:30 coordinator passed;12 UI passed,1 native-only skipped. Native Windows/D3D11 complete attempt7 passed12 methods but failed nativeUiEvidence because the controller was not exposed. This failure is retained and is not a pass. An unchanged-source isolated nativeUiEvidence attempt8 passed (3 including init/cleanup,0 failures), capturing25 screenshots and verifying six actual rendered red-image migrations, bounded admissions and final old-renderer retirement. Earlier complete native attempt6 passed12, predating the new picker-focus method. Functional native coverage combines attempt7's12 successful checks with attempt8's rendering proof; no single final complete native pass is claimed. Test-only QPA screens are used only offscreen and never alter OS displays.

Full Windows/Docker gates and follow-up independent approval remain pending. Physical color/HDR/refresh/mixed-DPI qualification remains MONITOR-VERIFY.

## Actual source approval and final narrow follow-up

Actual Sonnet5.5 review at6e22804 returned APPROVE SOURCE / FULL GATES PENDING (session e7d762ae-9ef5-471f-bcdd-8c62515579c8,533245ms,exit0,no permission denials). Complete result retained in review-6e22804. It requested a realistic native-frame-margin helper test before integration and complete native UI proof on the integrated commit. Physical monitor qualification remains unverified. The later complete native attempt9 at unchanged6e22804 passed13/13 in85426ms (whole-suite PNGs preserved separately); source provenance was recorded, but a pre-run binary hash was not captured historically. Production without BUILD_TESTING build72/72 and fresh26-frame exposed native runtime passed; generated input unchanged. These are supplementary proofs, not full Windows/Docker gates.

The follow-up adds the requested negative-origin work-area test with8/31/8/8 margins (initial and oversized client bounds plus reachable geometry retention). It also preserves maximized/minimized output state on unrelated appearance changes with regressions, uses the saving property instead of feedback-text matching, and explicitly reports unavailable connection type. Actual focused attempt16:

```text
1/2 Test #40: monitor_customization ............   Passed   11.19 sec
2/2 Test #41: monitor_customization_ui .........   Passed   41.98 sec
100% tests passed out of 2
Total Test time (real) =  53.46 sec
```

Qt totals:31 coordinator pass,12 UI pass,1 native-only skip. A narrow actual Sonnet follow-up and full locked gates remain pending for this last delta. Future native runs record the exact source and binary SHA256 before and after execution. Integration Docker command uses glancelot-browser:main under the same wrapper.
