# F054 delivery (partial): right-to-left layout and the translation hook

Lane Claude Code; branch `claude/f051`; reviewer from the Codex lane.

## Delivered
- Right-to-left: `Main.qml` mirrors the whole window with `LayoutMirroring` when the application layout direction is right-to-left (QGuiApplication follows the locale), and swaps the saved "left" and "right" panes for display only, so the tree sits on the start side and the saved layout stays untouched. Test: `rightToLeftMirrorsTheWorkspace` (toolbar order and tree side flip and flip back).
- Translation hook: `app/main.cpp` installs `:/i18n/glancelot_<language>.qm` when a catalog exists for `GLANCELOT_LANGUAGE` or the system language. No catalog is bundled yet.

## Not done
Wrapping the interface strings in `qsTr()` / `tr()` (hundreds across the QML and the controller, with tests that compare literal English text), the `lupdate`/`lrelease` build step and a first catalog, plural forms, locale-aware number and date formats beyond Qt defaults, and a native-speaker review of any translation. Per-glyph RTL checks (mixed-direction file names, bidi in the path bar, icon mirroring such as chevrons and "up") are unverified; only layout mirroring is tested.
