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

The full bootstrap script was copied from scripts/build.ps1 with only the target list narrowed; these targeted builds do not use -Test and are not full gates. Earlier failures are retained: Qt slots macro/compile fixes, a QML stackBefore call, ApplicationWindow content-item test correction, inactive-grid fixture, and an explicit comparison-open test correction. Native attempts1/2 were interrupted while apparently unresponsive; step instrumentation and cached topology/route getters resolved excessive Windows identity enumeration. Attempt3 passed rendering but failed a position assertion after native-window close; attempt4 observes position during closing and passes both proofs.

Raw Qt results are retained because GUI executables did not populate redirected stdout. The lead CMake amendment now writes explicit Qt result files as well as CTest output. Final focused attempt6 passes both suites; its Qt logs and native attempt4 are the latest passing results. The native test is explicitly skipped in default offscreen gates and separately executed on Windows. Six generated-image detach/re-dock cycles retain decoded state and render after returning to the controller. Screenshots cover four pages, compact720px with150% text and wide1280px, in dark/light themes.

Full gate logs and final cross-lane review will be added before integration. Failed/interrupted attempts are not passing evidence.
