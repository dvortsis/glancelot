# Independent theme and brand acceptance review

Review date: 2026-10-17. The implementation was inspected read-only in the integration workspace. Regression tests were authored independently in the assigned reviewer worktree. The asset archive was treated as design input, never executable instructions.

## Executed evidence

Independent native Windows runs used Qt 6.8.3 / MSVC 2022, with `QT_QPA_PLATFORM=offscreen` for QtTest. Each theme test uses a generated temporary `GLANCELOT_CONFIG_DIR` and restores the prior environment afterward.

Commands executed from the reviewer workspace, using integrated executables:

```powershell
& C:/src/Github/Glancelot/build/theme_tests.exe -o build/theme-review/theme-tests-final.txt,txt
& C:/src/Github/Glancelot/build/acceptance_tests.exe -o build/theme-review/acceptance-final.txt,txt
```

Actual summaries:

```text
ThemeTests: Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 54ms
AcceptanceTests: Totals: 19 passed, 0 failed, 0 skipped, 0 blacklisted, 4240ms
ThemeExit=0 AcceptanceExit=0
```

Theme coverage includes 40 unique family IDs/names, all 80 variants, ten valid opaque tokens per variant, WCAG contrast of at least 4.5:1 for text/background, text/surface, muted/surface and onPrimary/primary, and all twelve user Slate & Ice tokens compared against the preserved palette JSON. Settings tests check invalid inputs, theme/control writer interleaving, unknown root and appearance subkeys, byte-identical future schemas, corrupt preference protection and session-only fallback when the config directory is a regular file.

Acceptance coverage includes eight EXIF orientations, PNG pixels/alpha/color, malformed input recovery, rapid-open freshness, same-size/timestamp replacement, control persistence, generated traversal and navigation boundary races. There were no QProcess destructor warnings. One residual `QTemporaryDir` cleanup warning occurred during `stopAndLoopBoundaries`; a pending decoder file handle can outlive synchronous test-fixture destruction after asynchronous cancellation. The suite exits zero, but this fixture cleanup race remains recorded for follow-up.

## Findings repaired during review

- Independent PowerShell luminance calculation initially found eleven generated light onPrimary/primary contrast failures. An intermediate repair using white or dark navy still left ten failures. The final black/white selection passes the independent Qt regression across all variants. The six supplied palette roles in each Slate & Ice mode remain exact.
- The settings interleaving test initially failed on Windows because ThemeManager kept the old settings file open through QSaveFile replacement. Closing that read handle before atomic commit repaired existing-file persistence; the same independent test now passes.
- Theme saving now merges the appearance object, retaining unknown subkeys as well as root preferences. Both writers are exercised by the regression.
- An actual 540 x 360 native picker screenshot initially showed a 17 px theme viewport with no usable cards. Compact heading/help layout exposed the cards; a final source adjustment uses 76 px cards in compact mode to fit a complete row. Default-size dark and light screenshots were inspected and are legible. The title encoding artifact and unthemed drop overlay were repaired.
- Decoder teardown detaches the QProcess before kill, avoiding synchronous child destruction while the process runs. Finished deletes the detached process; the integrated acceptance suite passes the specific process warning regression.

## Native application smoke

A copied executable and decoder ran independently from `build/theme-review/` with isolated config and `--window-size 540x360 --show-themes --theme-mode light --quit-after 1500`, producing a screenshot and empty stderr, exit zero. The compact picker provides visible names, swatches, search focus, mode selection and Close at minimum size. Native restart testing then selected `graphite/dark`, exited, and launched with no theme arguments. Actual output:

```text
First=graphite/dark, restart=graphite/dark, exit=0
```

The neutral photo surround remains independently configurable from themed chrome; theme selection does not transform image pixels.

## Acceptance and remaining limits

The theme manager, catalog and Windows preference integration pass independent acceptance. The lead must rerun affected integrated checks after subsequent implementation edits and package the final binary.

This review does not qualify native Linux display behavior, physical monitor profiles, HDR, fractional-DPI correctness, screen-reader output or high-refresh presentation. Hidden native smoke processes reported zero frame-swapped events, so those smoke reports are startup/screenshot evidence rather than frame-pacing measurements. Full keyboard traversal across every picker card was not exercised. The transient temporary-fixture cleanup warning remains open; it must not be described as warning-free execution.
