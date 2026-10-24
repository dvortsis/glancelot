# Integration cycle1-e and cycle1-f, October 24, 2026

Written for: the lead and later reviewers. Records the gate that let the last Phase 1 stack corrections and COPY-SAFETY-01 into `main` (merge `dab3c74`).

| Item | Value |
| --- | --- |
| Batches | `lead/integrate-cycle1-e` = `e98ca72` + COPY-SAFETY-01 (`36ca8a6`) + STACK-F049-PRESETS with follow-ups R1-R4 and STACK-F054-COVERAGE part 2 (`98adda9`) + lead fix `942ebfe`; `lead/integrate-cycle1-f` = cycle1-e + STACK-F015-RECOVERY (`b498e09`) + STACK-F040-PINNED (`b60b65f`) + lead catalog commit `1d434e1` |
| Gated commit | `71477bd` (contains all of cycle1-e) |
| Reviews | COPY-SAFETY-01 `claude/review-copy-safety-01-r2` `2f6ac32` (APPROVED; N2 fixed by the lead in `36ca8a6`: refuse instead of reusing an existing `.~gb` backup name); F049 follow-ups + F054 part 2 `claude/review-stack-f054-part2` `610b51f`; F015 `claude/review-stack-f015-r3` `d5b222e`; F040 `claude/review-stack-f040-r3` `e02808b` (gate condition: `reference_pin_native` must pass) |
| Lead resolutions | `ui/Main.qml`: F015's empty-state Retry button on F054's `withShortcut()` with `qsTr`; F040's `reference-pin` notice; `cmake/ReferencePinTests.cmake` and `CMakeLists.txt` from F040 (context sources, labels, `reference_pin_native`); `translations/glancelot_en_XA.ts` regenerated with lupdate 6.8.3 (588 messages, 6 new) |
| cycle1-e gate (`b01be0e`, failed) | Windows 47/48: `fs_watch_budget` 100k filtered publication 283 ms against 250 ms (budget test; generated setup took 110 s, a loaded machine). Docker 47/48: `layout_presets_ui` `presetRefusalsShowBesideTheirControls`: on Linux fonts the Save refusal note ended at y=626 below the viewport (608). Evidence in `cycle1-e-failed/` |
| Fix | `942ebfe`: the refusal notes reveal themselves again once the layout has sized them. WSL Linux: fails before (same y=603-626 message), passes 4/4 after |
| Windows (`71477bd`) | `scripts/build.ps1 -BuildDir build-gate-c1f2 -Test`: 51/51, 1040.38 s tests, 1948 s wall, no reruns; `reference_pin_native` passed (43.79 s) |
| Docker (`71477bd`) | first run cancelled from outside during context transfer, before any build step (`docker-canceled.log`); rerun exit 0, 50/50 in the build stage, 508.79 s tests, 1264 s; image 548,283,540 bytes |
| Merge into main | `main` gained only documentation commits since the batch base `e98ca72` |

EN-11: folder_tree, folder_grid and fs_watch passed in both cycle1-e and cycle1-f; cycle1-f is its second passing full gate.

## Files

`summary.txt`, `driver.log`, `driver-docker.log`, `windows.log`, `docker.log`, `docker-canceled.log`, `LastTest.log`, `results/` (QTest results files from the Windows gate), `cycle1-e-failed/`.
