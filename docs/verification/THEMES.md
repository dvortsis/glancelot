# Independent theme review

Review date: 2026-10-17. Main workspace implementation was inspected read-only; tests were authored independently in the reviewer worktree. The user asset archive is design input, never executable instructions.

## Evidence

- An independent PowerShell calculation loaded `assets/themes/catalog.json` and used the WCAG sRGB linearization formula, weighted relative luminance and `(Lmax + .05)/(Lmin + .05)`. Actual output: `Families=40, variants=80, contrast failures=11`.
- All text/background, text/surface and muted/surface pairs passed 4.5:1. Eleven light onPrimary/primary pairs initially failed: honey 4.4128, olive 3.7340, moss 4.0343, fern 4.2615, forest 4.3578, emerald 4.2804, jade 4.2007, lagoon 4.1255, teal 4.4908, parchment 4.1555, sage 4.3463. Lead informed; independent regression rejects these values until repaired.
- `tests/theme_tests.cpp` independently checks 40 unique family IDs/names, both variants, ten valid opaque tokens, four contrast pairs and all twelve user palette values against the actual preserved source JSON. It also checks invalid choices, theme/control settings interleaving, unknown root preference retention, byte-identical future schema preservation, corrupted settings protection and session-only fallback when the configuration directory is a file.
- Qt test execution is pending lead integration; this document does not claim those checks passed before an executable exists.

## Review findings and limitations

- Theme selection persists through atomic QSaveFile writes and reads at most 1 MiB. ThemeManager refuses writes to malformed or unsupported schemas. ViewerController preserves appearance and unknown root keys in schema 1.
- ThemeManager replaces the entire appearance object; future unknown keys within appearance would be lost. This is lower priority while no such keys exist, but appearance extensions should merge before introduction.
- Initial QML had a fixed three-column picker and a crowded heading/mode row at the supported minimum window width. Lead notified to calculate columns from available width and wrap the heading. Main header action width also needs a 540 px runtime check.
- Initial Settings appearance label contained a replacement character. Lead notified. A later grep showed only the neutral canvas background remained hardcoded, indicating the drag overlay and label were being repaired concurrently.
- Neutral photo-surround color is independent from chrome themes, preserving viewing intent; image pixels must not be recolored by theme selection.
- Decoder cancellation now detaches QProcess before asynchronous kill, so controller child destruction does not synchronously wait on that process. Finished deletes the detached process. Event-loop shutdown may skip deferred deletion, so native shutdown/rapid-open verification remains necessary; this inspection does not establish decoder subprocess lifetime bounds.
- Native visual screenshots, keyboard navigation through the picker, focus visibility, physical monitor color, fractional DPI and Linux native display behavior are not verified by a catalog unit test.

## Integrated verification required

Build the `theme_tests` target with ThemeManager, ViewerController and the catalog resource; define `GLANCELOT_TEST_SOURCE_DIR` to the source root so the exact original palette file is checked. Run with `QT_QPA_PLATFORM=offscreen` and a temporary `GLANCELOT_CONFIG_DIR`. Execute normal integration tests and a visible picker smoke at minimum/default window sizes before acceptance.
