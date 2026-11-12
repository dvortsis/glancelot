# E6 handoff: high contrast follows the desktop, theme contrast minors, `--window-size` clamp

Slice E6 (first part), Phase 3 cycle 1 (`docs/handoff/PHASE3-CYCLE1-2026-11-12.md`). Branch `claude/e6-contrast`, one PR.

- **Base:** BASE_PLACEHOLDER
- **Tip:** the PR head (this document is in the tip commit; the reviewer pins the SHA from the PR).
- **ui-design-skills:** available and applied: application-interface-design (hierarchy, one choice per decision area, status not by colour alone, keyboard and screen-reader names), plus component-geometry and layout-spacing for the new row (existing `SettingRow`, `UiButton` segment and spacing tokens, no new geometry). Reviewed against the offscreen screenshots below at 1180x820 and 540x360.

## What changed

**F052/F053, native high-contrast detection (`src/app/system_contrast.hpp`, new, header-only).** Qt 6.8.3 has no contrast hint (checked: nothing in `QStyleHints`, `qnamespace.h` or `qpa/qplatformtheme.h`; `Qt::ContrastPreference` arrives in a later Qt), so the desktop is read natively:
- **Windows:** `SystemParametersInfoW(SPI_GETHIGHCONTRAST)` with `HCF_HIGHCONTRASTON`; the polarity of the active contrast theme from `GetSysColor(COLOR_WINDOW)` (Night sky, Aquatic, Dusk dark; Desert light). Live changes through a `QAbstractNativeEventFilter` on `WM_SETTINGCHANGE`+`SPI_SETHIGHCONTRAST`, `WM_SYSCOLORCHANGE` and `WM_THEMECHANGED`, coalesced by a zero-interval single-shot timer.
- **Linux/freedesktop:** `GTK_THEME` (`HighContrast`, `HighContrastInverse`, `…:dark`), GNOME's `org.gnome.desktop.a11y.interface high-contrast` (GNOME 42+) and the older `org.gnome.desktop.interface gtk-theme`. GSettings is resolved from `libgio-2.0.so.0` at run time with `QLibrary`, so nothing new is linked; every lookup goes through the schema source first (GSettings aborts on an unknown schema or key). Live changes through GSettings `changed::<key>` signals, which Qt's GLib event loop delivers on the GUI thread.
- **Everywhere:** a re-read on `QStyleHints::colorSchemeChanged`. The offscreen and minimal platforms report "no desktop" (deterministic tests on any developer machine); tests install a probe through `setSystemContrastProbeForTesting`.
- Reading is a system call or a few in-memory GSettings reads on the GUI thread; no decode, directory scan or network.

**Wiring without a header change (`src/app/theme_manager.cpp`).** `ThemeManager` owns a `SystemContrastWatch` child and emits `changed()` when the reading changes. `colors()` gains one non-colour key, `systemHighContrast`. `dark()` takes the Windows contrast theme's polarity while Mode is "Follow system"; an explicit Dark or Light wins; with no stated polarity (GNOME) the colour scheme decides as before. `include/glancelot/theme_manager.hpp` is unchanged (see the open questions).

**Override in the theme picker (`ui/ThemePicker.qml`).** A "High contrast" row leads the palette gallery: **Follow system** (default) or **Always on**, as a two-button segment like Text size. It writes the existing persisted `viewer.highContrast`, so "Reset appearance" and "Restore all" already clear it. The description says whether the desktop's setting is on, and while high contrast is on a note explains that the palette applies when it is off. Each button has an accessible name; the desktop state is in the first button's accessible description.

**Tokens (`ui/Theme.qml`).** No token renamed. `highContrast` = `highContrastAlways` (viewer) or `systemHighContrast` (desktop), both new read-only tokens. The existing AAA high-contrast set is unchanged and now applies in both cases.

**Theme contrast minors, through tokens (`src/app/theme_contrast.hpp`, new, header-only).** `ThemeManager::preview()` and `colors()` return the catalog roles byte-identical (branding; `themes` still checks slate-ice against `assets/brand/palette.json`) plus three derived roles:
- `controlBorder`: the catalog border mixed toward the family's text colour, in 1% sRGB steps, to the first mix that reaches 3:1 against both background and surface (WCAG 2.2 SC 1.4.11). Exposed as the new `Theme.controlBorder`. `Theme.border` keeps the brand value, because it draws decorative separators (which 1.4.11 exempts, as `theme_tests` already notes) and `grid_ui_tests` pins the top-bar edge and divider to it.
- `familyHover`: mixed from the family's own surface and text. It replaces the value of `Theme.hover` (same name), so hover, pressed and the grid and filmstrip "Loading preview"/"Preview unavailable" tiles follow the family. The catalog `hover` is one fixed blue-grey for all 40 families (UX audit finding 19).
- `placeholder`: a step stronger than `familyHover`, as the new `Theme.placeholder`.
- Neither fill lets text or muted text fall below 4.5:1, or the primary marker below 3:1.

The palette cards and their swatches draw `controlBorder`.

**`--window-size` (`app/main.cpp:296` only).** The value is validated, clamped to the window's screen `availableGeometry()` and the 540x360 minimum, and a clamp is reported on stderr (`Glancelot: --window-size 99999x99999 is outside the 540x360 minimum or the 800x800 screen area; using 800x800`). Malformed values never reach that line: B8's early parser (`app/startup_intent.cpp`) already refuses them with exit 2 and its `ArgumentError` record. The line keeps a backstop that prints to stderr and leaves the size alone. 99999x99999 now starts and quits in 1.4 s offscreen, where the audit measured more than 25 s.

## Measured contrast, before and after (all 40 families)

From `theme_contrast_tests::derivedRolesMeetContrastInEveryVariant` (qInfo in the results file); minimum over the families unless a range is given.

| Pair | Light before | Light after | Dark before | Dark after |
| --- | --- | --- | --- | --- |
| Control boundary vs background and surface (`border`, then `controlBorder`) | 1.18 | 3.00–3.08 | 1.53 | 3.00–3.09 |
| Hover fill vs surface (`hover`) | 1.18 (one fixed colour) | 1.23 (per family) | 1.44 (one fixed colour) | 1.45 (per family) |
| Placeholder tile vs surface (`hover`, then `placeholder`) | 1.18 | 1.23 | 1.44 | 1.70 |
| Muted label on placeholder tile ("Loading preview") | 5.44 | 4.51 | 5.56 | 4.69 |
| Text on hover / placeholder | ≥10.2 | ≥4.5 asserted | ≥8.8 | ≥4.5 asserted |

SAMPLES_PLACEHOLDER

In light themes, the placeholder can't be stronger than about 1.23–1.43:1 without taking the shared muted text below 4.5:1, because `muted` is one fixed colour across the catalog. The tile is identified by its label, so 1.4.11 doesn't require 3:1 for the fill.

## Screenshots (offscreen, generated)

In `docs/verification/E6/`:
- `app-themes-light.png`, `app-themes-dark.png`, `app-themes-high-contrast-light.png` and `app-themes-high-contrast-dark.png` come from the app. Run with `glancelot --no-resume --show-themes --theme-mode <mode> --window-size 1180x820 --screenshot …` on `QT_QPA_PLATFORM=offscreen:configfile=` with a 1600x1000 screen; high contrast is `{"schema":1,"highContrast":true}` in a disposable `GLANCELOT_CONFIG_DIR`.
- `theme-picker-*.png` come from `GLANCELOT_E6_UI_EVIDENCE=<dir> high_contrast_ui_tests`. That covers light, dark, "Always on" high contrast in both modes, and the *faked* desktop high contrast in both modes.

Physical high contrast is **unverified**: Windows contrast themes, GNOME High Contrast, Narrator and Orca can't run in the cloud. `docs/E6-HIGH-CONTRAST-RUNBOOK.md` is the packet for the user, with a results table.

## Files changed (grant check)

GRANT_PLACEHOLDER

## Tests

New targets in `cmake/E6Tests.cmake` (one `include` line in the root `CMakeLists.txt`):
- `theme_contrast` (label `process`), 10 functions:
  - derived roles in all 80 variants, with catalog roles unchanged;
  - family-following hover;
  - the step search, and an incomplete palette;
  - live follow with no notification on an unchanged reading and no settings write;
  - polarity only in Follow-system mode;
  - watches end with their manager;
  - GTK theme names;
  - the native reading is safe here;
  - `--window-size` clamp to the screen and to 540x360, and an in-range size untouched;
  - malformed sizes refused (exit 2, no report).
- `high_contrast_ui` (label `ui`), 5 functions, on the shipped `Theme.qml` and `ThemePicker.qml`:
  - desktop high contrast on and off while running, in both modes, with AAA pairs and no preference written;
  - Windows polarity;
  - "Always on" by pointer, persisted and immune to desktop changes; "Follow system" by keyboard, handing control back;
  - tokens in all 80 variants as drawn;
  - card and swatch boundaries.

FAILBEFORE_PLACEHOLDER

CTEST_PLACEHOLDER

## Failure and cancellation cases covered

- No GIO, no schemas, or no key: no high contrast, and no abort (lookups go through the schema source first).
- Offscreen and minimal platforms read no desktop.
- A probe that returns the same state twice does not notify.
- A destroyed `ThemeManager` is never called (its watch leaves the registry in its destructor; `watchesEndWithTheirManager`).
- A burst of Windows messages is coalesced into one reading.
- Malformed and out-of-range `--window-size`.
- An incomplete palette passes through unchanged.
- Async rule: every reading is synchronous on the GUI thread, so there is no stale result to order. A later reading always replaces the earlier one in the same call.

## Decisions taken

1. **No header change; desktop state carried in `colors()` plus a child watch.** The alternative was new `Q_PROPERTY`s on `ThemeManager` (`systemHighContrast`, and a `contrast` preference). That is cleaner, but `include/glancelot/*` changes are an open question for the lead this cycle. A QML singleton registered from `theme_manager.cpp` was rejected: it would need Qt Qml in every target that compiles it, which means root CMake edits.
2. **The override is "Follow system" / "Always on" through the existing `viewer.highContrast`.** A third choice, "Off even when the desktop is on", needs a new persisted value. That means a header or a `viewer_controller` change, neither in this grant (open question 1). On Windows, apps are expected to honour the contrast theme, so the missing case is the rare one.
3. **The override row lives in `ThemePicker.qml`**, the only Settings file this slice owns. It sits under the existing "Palette" heading, because high contrast replaces the palette. A two-button segment rather than a combo box: it shows both choices at once, and `grid_ui_tests::auditEverySettingsPageAndChoiceMenu` counts exactly eight Settings menus.
4. **Linux through GSettings loaded at run time, not the XDG portal over QtDBus.** The portal (`org.freedesktop.appearance contrast`) would need `Qt6::DBus` linked into every target that compiles `theme_manager.cpp` (lead-owned root CMake). GSettings covers GNOME 42+ directly, and older GTK desktops through the theme name. KDE and Xfce have no standard flag: they get "Always on", recorded in the RUNBOOK.
5. **Windows polarity from `COLOR_WINDOW`, with the app's own AAA token set** rather than the contrast theme's exact system colours ("forced colours"). The existing set is checked at 7:1 and keeps the brand geometry. Mapping `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT` and the rest is a possible follow-up (open question 3).
6. **`Theme.border` keeps the brand value; the 3:1 boundary is the new `Theme.controlBorder`.** Raising `border` itself was the alternative. It would darken every decorative separator, break `grid_ui_tests`' top-bar checks (pinned to the catalog border), and move away from the supplied palette. The visible effect this PR is limited to the palette cards (open question 2).
7. **`Theme.hover` changes value, not name, to the family-derived fill.** This fixes finding 19 everywhere hover and the preview tiles are drawn, without touching `PhotoGrid.qml` or `Filmstrip.qml`. The target steps (1.25/1.45 for hover, 1.4/1.7 for the placeholder, light/dark) stay close to the current dark values and visibly above the light ones, within the 4.5:1 text guard.
8. **`--window-size`**: clamping is reported on stderr (useful to automation) and doesn't fail the run. Malformed input stays the early parser's exit 2 (B8's file, unchanged). The main.cpp backstop only prints.
9. **Native reading is skipped on the offscreen and minimal platforms**, so a developer whose desktop has high contrast on still gets deterministic tests; the platform paths are exercised by `nativeReadingIsSafeHere` and the RUNBOOK.

## Open questions for the lead

1. **Header delta for a full tri-state override.** Approving this, or moving it to `viewer_controller`, enables a third picker choice "Off" (honour the app's palette even when the desktop is in high contrast) and a non-colour key out of `colors()`. The delta, on `include/glancelot/theme_manager.hpp`:
   - `Q_PROPERTY(bool systemHighContrast READ systemHighContrast NOTIFY changed)`;
   - `Q_PROPERTY(QString contrast READ contrast WRITE setContrast NOTIFY changed)` with `"system" | "high" | "standard"`, persisted as `appearance.contrast` and replacing `viewer.highContrast` with a migration.
2. **Adopt `Theme.controlBorder` in the shared controls.** This makes the 3:1 boundary visible app-wide. The files are outside this grant, and each change is one token name:
   - `ui/UiTextField.qml:27`;
   - `ui/UiComboBox.qml:33` and `:83` (popup);
   - `ui/UiButton.qml:76` (outline variant);
   - `ui/UiDialog.qml:50`;
   - the grid filter field, `ui/PhotoGrid.qml:378`.

   `PhotoGrid.qml:784` and `Filmstrip.qml:123` could also move their preview tiles to `Theme.placeholder` (a hovered grid cell and its tile share `Theme.hover` today). Decorative separators keep `Theme.border`.
3. **Forced colours on Windows:** should a later slice map the user's own contrast theme colours (`GetSysColor`) instead of the app's black and white set? That is what Windows users of custom contrast themes expect, but it needs a physical check per theme.
4. **Early parser message:** malformed `--window-size` is reported as JSON on stdout with exit 2 (`app/startup_intent.cpp`, B8). Should B8 also print a human sentence on stderr?

## Unverified

- Physical Windows contrast themes, GNOME High Contrast, KDE and Xfce behaviour, Narrator and Orca: see `docs/E6-HIGH-CONTRAST-RUNBOOK.md`.
- The Windows build compiles `system_contrast.hpp` with `<windows.h>`; the hosted `windows-2022` `viewer` job is the compile and test check (see the PR).
- The cloud's Qt is conda-forge's 6.8.3 build, because `download.qt.io` is blocked by this environment's network policy (aqt failed with a proxy 403). It's the same Qt version, but not the aqt archives the bootstrap script installs.
