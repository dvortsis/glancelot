# MONITOR-CUSTOM-01 integration handoff — October22,2026

The user reassigned implementation from Claude to Codex and appointed this session lead. Implementation uses branch codex/monitor-custom-01, worktree .worktrees/codex-monitor-custom-01 and build-codex-monitor-custom-01. Reviewed source/tests: a0b00ff5aa8c946b1b510355034aa3c6c07b4525. Main-synchronized gated source: 3f570dc3f9319267ff21054e39e68fcd21abcdb9. Integration merge: pending no-ff merge. Queue closeout: recorded separately after integrated checks.

The component adds controller display selection/identify/move/swap/recovery, output appearance and routing policy, generation-fenced linked or independent viewports, four real detachable panes, canonical freeze/layout lock, and bounded per-slot presentation/workspace presets with atomic settings/SAFE integration. Pane/model/decoder instances are retained. Output window state survives appearance changes; emergency actions restore reachable controls. Production wiring is unconditional and also builds with BUILD_TESTING=OFF.

Actual independent Claude Code Sonnet5.5 approved source at6e22804 (session e7d762ae-9ef5-471f-bcdd-8c62515579c8), then final delta ataf339f9 (session fcc1ab9f-4a81-4dbc-b2d9-18b72e945d4a). Earlier changes-required reviews and failed/interrupted test attempts remain archived. These were actual read-only cross-lane CLI reviews, not Codex self-reviews. Subsequent compact Settings fix70a6903 (session f186607c-11f3-416f-9e40-dabb9e85789b) and test-only native fixturea0b00ff (session f6bcf65f-83fb-4740-a4c9-4adf8f8801fd) were also independently approved. All source approvals left full gates pending. The requested helper test and complete native proof have since been supplied.

[Raw evidence archive](../verification/monitor-custom-01/codex-2026-10-22/README.md) includes commands, source diffs, actual review results, all failed/passing iterations, native PNGs and Git-blob SHA256 manifest. [Canonical final source review](../verification/MONITOR-CUSTOM-01-CLAUDE-REVIEW.md). No failed attempt is reported as passing.

## Full gates and exact source equality

Under lead integration lock codex-lead-monitor-custom-01, main merged cleanly into accepted branch. Git object equality proves app/include/src/ui/tests/CMakeLists.txt/cmake match revieweda0b00ff. Full gate ran serially in one main-checkout wrapper block (MSVC and project Qt6.8.3 bootstrap in the retained script):

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-custom-01 -Run {
    cmake -S . -B build-codex-monitor-custom-01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGLANCELOT_BUILD_VIEWER=ON -DCMAKE_PREFIX_PATH=<project-local Qt6.8.3>
    cmake --build build-codex-monitor-custom-01 --target glancelot --parallel 4
    & ./scripts/build.ps1 -BuildDir build-codex-monitor-custom-01 -Test
    # Explicit exit checks stop the block on any failure.
    docker build --progress=plain -t glancelot-browser:main .
}
```

Actual production OFF build output:

```text
[8/62] Running moc --collect-json for target glancelot
[9/12] Generating .rcc/qmlcache/glancelot_ui/SettingsDialog_qml.cpp, .rcc/qmlcache/glancelot_ui/SettingsDialog_qml.cpp.aotstats
[10/12] Building CXX object CMakeFiles\glancelot.dir\build-codex-monitor-custom-01\.qt\rcc\qrc_glancelot_raw_qml_0.cpp.obj
[11/12] Building CXX object CMakeFiles\glancelot.dir\build-codex-monitor-custom-01\.rcc\qmlcache\glancelot_ui\SettingsDialog_qml.cpp.obj
[12/12] Linking CXX executable glancelot.exe
```

Actual full Windows output:

```text
      Start 39: recovery_process
39/41 Test #39: recovery_process .................   Passed   29.60 sec
      Start 40: monitor_customization
40/41 Test #40: monitor_customization ............   Passed    8.62 sec
      Start 41: monitor_customization_ui
41/41 Test #41: monitor_customization_ui .........   Passed   23.00 sec

100% tests passed out of 41

Total Test time (real) = 627.53 sec
```

Actual Docker output (fresh product source layer; cached Qt base permitted):

```text
#26 159.7 11/41 Test #11: folder_grid ......................   Passed  126.84 sec
#26 339.7 40/41 Test #40: monitor_customization ............   Passed    7.40 sec
#26 351.5 41/41 Test #41: monitor_customization_ui .........   Passed   11.88 sec
#26 351.5 100% tests passed, 0 tests failed out of 41
#26 351.5 Total Test time (real) = 351.28 sec
#37 naming to docker.io/library/glancelot-browser:main done
```

The first full Windows attempt at7f887ed failed40/41 solelyfolder_grid; Docker did not run. Its complete output remains in full-gates-attempt-1. The compact Settings entry button had reduced the palette viewport; it was moved into Browsing. The unchanged regression then passed3/3 and full folder_grid passed74 Qt methods (4 screenshot-only skips) in213.49s. A stricter native fixture explicitly raises/activates the window and computes click coordinates after capture; no exposure/popup assertion was relaxed. Failed native attempts11 and its compressed signal trace remain retained.

Raw complete final configure/build/CTest/Docker streams and LastTest log are in full-gates. Actual wrapper exit0, phase complete, source3f570dc; Docker image sha256:b27b2eae7d6cd52b9d285b5cee4ffd1c0b4576aac0a48cb551cd57c7747123e6 (linux/amd64).

## Native and integrated confirmation

Final candidate Windows/D3D11 UI suite:

```text
Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 56738ms
```

Candidate source a0b00ff and binary SHA256 were identical before/after. The proof captures28 dark/light compact720px150% text and wide1280px PNGs, verifies six actual red-image GPU migrations and bounded aggregate admission with final old-renderer retirement. Focus/scroll/selection/content, actual key/pointer/palette recovery and import/reset run against real shipped QML and real pane instances.

A disposable probe linked to the same approved component objects observed two Windows QScreens and passed actual windowed frame fit on negative-origin work area, appearance preserving maximized state, and reachable restored frames after maximized/minimized moves. Its failed first compile (missing MSVC language flag), corrected build/run, full source and actual JSON are retained. This proves observed window geometry only.

Checks on exact integration commit pending no-ff merge, whose product objects match the gated candidate:

```text
Pending exact integration commit checks.
```

Native integrated binary/source hashes and complete output, current screenshots and isolated generated-media production report are retained in integrated-proof. Generated input hashes match before/after, and no user media/preferences were used.

## Limits and follow-up

Physical mixed-DPI/refresh/color/HDR, output accuracy, compositor-specific behavior and fullscreen controller swap remain MONITOR-VERIFY-01 qualification. Two enumerated Windows QScreens and offscreen synthetic1/2/3 topology tests do not establish that matrix. Qt does not expose connection type here; UI reports it unavailable. Settings search may reach the command through Controls instead of switching directly to the Browsing button; the source reviewer accepted this discoverability gap as nonblocking. Browser/unsupported compositor output remains unsupported.

Accepted nonblocking review observations remain explicit: emergency recovery is runtime placement recovery and can restore prior detached preferences on restart until a later save; bounded retry unit coverage remains counter-level while native renderer lifetime is separately exercised; Windows display enumeration is bounded/cached but may be costly; repeated accessible pane action names and compact-page polish remain. No original media is modified. The task remains integration and the lead lock remains held until merge and integrated checks finish.
