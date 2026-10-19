# F054 delivery (partial): right-to-left layout and the translation hook

Lane Claude Code; branch `claude/f051`; reviewer from the Codex lane.

## Delivered
- Right-to-left: `Main.qml` mirrors the whole window with `LayoutMirroring` when the application layout direction is right-to-left (QGuiApplication follows the locale), and swaps the saved "left" and "right" panes for display only, so the tree sits on the start side and the saved layout stays untouched. Test: `rightToLeftMirrorsTheWorkspace` (toolbar order and tree side flip and flip back).
- Translation hook: `app/main.cpp` installs `:/i18n/glancelot_<language>.qm` when a catalog exists for `GLANCELOT_LANGUAGE` or the system language. No catalog is bundled yet.

## Not done
Wrapping the interface strings in `qsTr()` / `tr()` (hundreds across the QML and the controller, with tests that compare literal English text), the `lupdate`/`lrelease` build step and a first catalog, plural forms, locale-aware number and date formats beyond Qt defaults, and a native-speaker review of any translation. Per-glyph RTL checks (mixed-direction file names, bidi in the path bar, icon mirroring such as chevrons and "up") are unverified; only layout mirroring is tested.

## Update: interface strings wrapped (October 19, 2026)

135 plain QML text literals (`text`, `placeholderText`, `label`, `description`, `heading`, `tip`, `title` assigned a whole string) in `DeleteFlow`, `FolderTree`, `Main`, `PaneArrangement`, `PhotoGrid`, `SettingsDialog` and `UiDialog` are now `qsTr("...")`. With no catalog installed `qsTr` returns the source text, so behaviour and every English-comparing test are unchanged (full suite 20/20 on Windows; Linux Docker 20/20 before this pass).

Still not done, in order: strings built by concatenation or ternaries (for example status messages and "Move N items to the Recycle Bin"), which need `qsTr` with `%1` arguments and plural forms; strings in the controller and models (`status`, command labels in the registry, error messages); the `lupdate` extraction and `lrelease` build step, which cannot run here because the project-local Qt has no Linguist tools (`lupdate.exe` and `lrelease.exe` are absent); a first catalog and native-speaker review.

## Update: command names and the catalog build step

Command names now pass through `QCoreApplication::translate("Commands", <English>)`, so a catalog can translate every command label in Settings, the palette and context menus; an untranslated name falls back to English (test: `commandNamesAreTranslatable`, using a stand-in translator). `CMakeLists.txt` builds `translations/glancelot_*.ts` into `:/i18n` through `qt_add_translations` only when Qt Linguist tools are installed (`translations/README.md` explains the workflow); without them nothing changes. Still open: strings built by concatenation or with plurals, controller status and error messages, running `lupdate` and a real catalog (Linguist tools are not installed in this environment, so this step could not be exercised end to end).

Update: 18 plain controller status messages (`status = "..."`) now go through `QCoreApplication::translate("Status", ...)`. Window-slot, acceptance, folder-tree, theme, settings and input UI tests pass unchanged. Statuses built by concatenation (they carry file or folder names), the error strings in the decoder and models, and plural-aware text remain.
