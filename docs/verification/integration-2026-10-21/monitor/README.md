# Monitor integration evidence, October 21, 2026

Synchronized source candidate: `b128be91baaeb473ee380a8cbd16d47b1efe462c`, branch `codex/monitor-output-r2`, merging main `7c0bd2e` (accepted integrated INPUT plus evidence) into the independently accepted monitor correction stack. Only conflict: `image_canvas.hpp` includes; retain QVariantMap, QPointer and QQuickWindow. No interface contract or requirement was weakened.

Actual independent Sonnet 5.5 session `7acd6430-5aa3-40ac-8d95-a360afff1bd4` (see actual JSON for authoritative session spelling) ACCEPT, exit0, all nine native UI skills read. `CLAUDE-ACCEPT.md` and `claude.json` retain the unedited source-only review and its coverage limits.

`merge-source-equivalence.json` verifies 18 Git blobs exactly. Monitor display-manager API/implementation, presentation window API/implementation, output-color worker, shader and monitor tests equal accepted `9b03718`. INPUT Main/PhotoGrid/FolderTree/CommandMenuItem, input/grid/window-slot tests, root CMake, decoder worker and watcher headers equal integrated main `7c0bd2e`. No mismatches. The combined ImageCanvas source and controller command-table deltas were reviewed independently.

Actual full gate wrapper: `gate.ps1` (Windows exceptions and Docker native exit status checked separately), run from this candidate worktree:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-output-r2 -Run {
    & ./scripts/build.ps1 -BuildDir build-codex-monitor-output-r2 -Test
    docker build --progress=plain -t glancelot-browser:main .
}
```

Raw Windows and Docker output: `windows.txt`, `docker.txt`; their status files are authoritative. A missing status file is pending, never passed.

Non-blocking independent-review advisories remain: primary-canvas hold restoration after resize/DPR change can retain stale saved geometry; a direct beginHold call without a valid watcher or destruction of its window can leave hold state until another view operation. Presentation canvases do not use hold. Prior monitor advisories about budget-test naming and headless first-visit coverage remain documented. No source change was requested in this integration confirmation.

Physical ICC calibration, HDR, input-to-photon latency, real keyboard layouts/AltGr and hotplug/suspend/GPU-loss recovery remain unverified. Native generated-pixel/state probes, when run, are recorded separately and do not establish those hardware claims.


## Completed qualification

Accepted synchronized `b128be9`: full Windows 34/34 in 659.04 seconds, exit 0; full Linux Docker 34/34 in 350.52 seconds, exit 0, runtime image export completed. These are fresh candidate checks, not cached CTest output.

Integrated main commit: `8e8871a2832478cf652c764409994c261f01184e` (`--no-ff`). Entire tree equals qualified `b128be9`, recorded by `git diff --exit-code b128be9 8e8871a` returning0. No product change occurred between qualification and merge.

Exact integrated commit focused checks:3/3 in 91.35 seconds, exit 0 (input_ui, viewport, monitor_output). Native opt-in:4 passed, 0 failed, 0 skipped in 892 ms, exit 0. The generated Display-P3 patch matches Qt's reference; viewport source centers/physical zoom restore across attached DPR 1/2 displays, status P27h-20. This is not physical calibration/HDR qualification.

Actual commands are in `integrated-checks.ps1`; complete outputs/status are `integrated-ctest*` and `integrated-native*`:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-output-r2 -Run {
    ctest --test-dir build-codex-monitor-output-r2 -R '^(input_ui|viewport|monitor_output)$' --output-on-failure
    # Native probe: QT_QPA_PLATFORM=windows; GLANCELOT_MONITOR_NATIVE_COLOR_PROOF=1;
    # QSG_RHI_BACKEND=d3d11; QSG_RHI_PREFER_SOFTWARE_RENDERER=1; QT_QUICK_BACKEND unset
    & ./build-codex-monitor-output-r2/monitor_output_tests.exe testNativeGpuColorPatch testNativeDisplayIdentityStateRestore -o .cache/integration-monitor/integrated-native.txt,txt | Out-Null
}
```

Full gates are on the accepted candidate; the integration commit has exact tree equivalence plus focused/native checks. No second full suite on the merge commit is claimed. Two empty authoring artifacts inherited from the monitor candidate, root `o.txt` and `o2.txt`, are removed in the documentation closeout; this changes no product/build/test source.
