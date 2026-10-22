# F053 contract and delivery: high contrast and large touch targets

Lane Claude Code; branch `claude/f051` (stacked; same branch as F051 because both edit the new Settings dialog); reviewer from the Codex lane.

## Frozen interface (approved additions)
`ViewerController` properties `highContrast` and `largeTargets` (bool, persisted as `highContrast` and `largeTargets`, cleared by "Reset appearance" and "Restore all"). `Theme.qml` reads them like `textScale`; no component changes.

## Behaviour
- High contrast replaces the colour roles with black/white surfaces and text, a text-coloured border and focus ring, a high-contrast primary (yellow on dark, deep blue on light) and strong danger colours, and widens borders to 2 px and the focus ring to 3 px. `input_ui_tests` checks WCAG AAA (7:1) for text, muted text, text on primary, borders, focus ring and danger against both background and surface in dark and light, and 3:1 for primary against the background.
- Large targets raise `controlHeight` and `rowHeight` to 44 px and the bar to 60 px, so every control built on the tokens grows together. The test checks a real toolbar button and every visible context-menu row.
- Both options are switches in Settings → Appearance, after Reduced motion, and work with text scale.

## Not done
Native high-contrast detection (Windows High Contrast, forced colours), a contrast check of the photo canvas overlays over arbitrary photos, touch gestures, and screen-reader trials (unverified). Bar-heavy layouts at 60 px bars may need the pane arrangement minimums revisited at the 540x360 floor; this was not measured.
