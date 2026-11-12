# E6 RUNBOOK: physical high-contrast qualification (F052/F053)

Written for: the user, on real desktops. The cloud build can only fake the desktop's reading (`setSystemContrastProbeForTesting` in `high_contrast_ui_tests` and `theme_contrast_tests`), so everything below is **unverified** until it is run here and the results table is filled in. No offscreen result counts as one of these.

What E6 claims, and what this checks:
- Glancelot reads the desktop's high-contrast setting at start and follows it while running, without a restart.
- In Settings > Appearance > Palette > High contrast, **Follow system** (the default) uses high contrast exactly while the desktop's setting is on; **Always on** keeps it on whatever the desktop says.
- On Windows, the black or white token set follows the high-contrast theme's window background while the Mode is **Follow system**; an explicit Dark or Light mode wins.
- Nothing is written to `settings.json` when only the desktop changes.

Use a build of the E6 commit (or later). Start each case with a fresh preferences folder so earlier choices don't leak in: set `GLANCELOT_CONFIG_DIR` to an empty folder, then open a photo folder with a few generated images (`scripts/generate-fixture.py`).

## Windows 10 and 11

Windows: Settings > Accessibility > Contrast themes (Windows 11), or Ease of Access > High contrast (Windows 10). Left Alt + Left Shift + Print Screen toggles the last theme.

| # | Steps | Expected |
| --- | --- | --- |
| W1 | Turn on **Night sky** (or Aquatic or Dusk). Start Glancelot. | Black backgrounds, white text, yellow selection and focus, 2 px borders, 3 px focus ring. Settings > Appearance shows "Your desktop's high-contrast setting is on…" and the high-contrast note above the palette search. The title bar uses the system colours. |
| W2 | With Glancelot open, switch to **Desert**. | Within a second, without restarting: white backgrounds, black text, deep-blue selection. |
| W3 | With Glancelot open, turn contrast themes **off**. | The selected palette (default Slate & Ice) returns, in the desktop's light or dark mode. The note disappears. |
| W4 | Settings > Appearance > High contrast > **Always on**. Turn the desktop contrast theme on, then off. | High contrast stays on throughout. The "Modified from default" dot shows on the High contrast row. |
| W5 | Choose **Follow system** again. | With the desktop off, the palette returns. |
| W6 | Turn on Night sky, then set Mode to **Light**. | The white high-contrast set (explicit mode wins over the theme's polarity). Back to Follow system: black again. |
| W7 | After W1–W6, open `settings.json` in the preferences folder. | `highContrast` is `true` only if W4 left it on and is absent or `false` after W5; there is no other key that changed because the desktop changed. |
| W8 | With Narrator running, Tab to the two High contrast buttons. | Narrator reads "High contrast: follow system" (with whether the desktop setting is on) and "High contrast: always on", and the selected one as selected or pressed. |

## Linux (GNOME 42 or later)

GNOME: Settings > Accessibility > High Contrast, or `gsettings set org.gnome.desktop.a11y.interface high-contrast true`.

| # | Steps | Expected |
| --- | --- | --- |
| L1 | Turn High Contrast on. Start Glancelot (native Wayland or X11, not offscreen). | High-contrast tokens; dark or light follows the desktop's Style (Qt's colour scheme), because GNOME's setting carries no polarity. |
| L2 | With Glancelot open, turn High Contrast off, then on. | It follows both ways without a restart. |
| L3 | Older GNOME or another GTK desktop: set the GTK theme to `HighContrast`, then `HighContrastInverse` (`gsettings set org.gnome.desktop.interface gtk-theme HighContrast`). | White set for HighContrast, black set for HighContrastInverse, while Mode is Follow system. |
| L4 | Start Glancelot with `GTK_THEME=HighContrast`. | High contrast from the first frame. |
| L5 | With Orca running, repeat W8. | Orca reads both buttons and the selected state. |

KDE Plasma, Xfce and other desktops have no standard high-contrast flag that Qt 6.8.3 or GSettings exposes; record what you see, but "not detected" there is expected, and **Always on** is the way to get high contrast.

## Results

| Case | Date | OS, version, desktop | Build (commit) | Result (pass/fail and what you saw) |
| --- | --- | --- | --- | --- |
| W1 | | | | |
| W2 | | | | |
| W3 | | | | |
| W4 | | | | |
| W5 | | | | |
| W6 | | | | |
| W7 | | | | |
| W8 | | | | |
| L1 | | | | |
| L2 | | | | |
| L3 | | | | |
| L4 | | | | |
| L5 | | | | |
