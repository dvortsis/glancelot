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

Update: the concatenated status and conflict messages that name a file, folder or command now use `%1` arguments through the `Status` context ("Opening %1…", "Shortcut conflicts with %1…", "Sorted by %1%2%3.", the clipboard-copy and recorder messages). Counted messages keep separate singular and plural sentences, which a catalog can translate independently; true plural forms (`%n`) and the decoder, model and delete-flow error strings remain.

Update: the pane-layout feedback (`Layout` context), theme and preference errors (`Theme`) and the file-operation messages shown in delete results (`Delete`, worker-thread safe) are now translatable, 26 more plain strings. Remaining: strings in `directory_entry_model`, the decoder and the QML `DeleteFlow` that are built from counts and names (they need `%n` plural forms), and the catalog itself.

Update: the counted interface strings (grid item count, delete confirmation, notice and result titles, the grid context-menu delete row) now use `qsTr` with `%1`/`%2` placeholders and separate singular and plural sentences, with identical English output (grid_ui 74/74, input_ui 28/28). A catalog supplies its own plural handling per sentence. Remaining: model and decoder error strings, and the catalog (needs `lupdate`, which this environment lacks).

Update: the decoder's fixed English messages are translated in the viewer (`Decoder` context, English text as the key; messages with a codec error tail fall back to English), and the grid model's folder-error and selection-summary strings use the `Grid` context. Full suite 23/23 on Windows. What is left of F054 is only the catalog itself (needs `lupdate`/`lrelease`, not installed here) and native-speaker review.

## Update: the catalog pipeline now runs end to end (Linux, `qttools` from aqt)

`lupdate` was run in the Linux build image after installing `qttools`: it extracted 209 source strings into `translations/glancelot_en_XA.ts`, a pseudo-locale (accented, bracketed, placeholders kept), and `lrelease` compiled it. `CMakeLists.txt` now compiles every `translations/glancelot_*.ts` with `lrelease` whenever that program is found (the Qt Linguist CMake package is not required, and it is absent from the aqt download) and embeds the result under `:/i18n`. In the Linux image, `GLANCELOT_LANGUAGE=en_XA` loads the embedded catalog silently, and `GLANCELOT_LANGUAGE=zz` prints that no catalog exists. Windows builds with the project-local Qt skip the step (no `lrelease` there). Not verified: that translated text actually renders in the running window (only the load is checked), plural handling, and any real translation. Strings looked up at run time by English text (command names, status, decoder, grid, layout, theme and delete messages) are not seen by `lupdate`; `translations/README.md` says how to add them.

## Runtime evidence

`docs/verification/F054-pseudo-locale.png` is the empty main window rendered in the Linux build image with `GLANCELOT_LANGUAGE=en_XA` (offscreen, 900x600). Two English runs are byte-identical and the `en_XA` run differs; the picture shows translated text (Open, Folders, Settings, Previous, Next, Fit, Rotate, the title and tagline) in brackets with accents, and, as intended, the strings that are still not translatable stay plain: the Photo/Grid/Both view switch, the sort button label, "No photo open", "Open a photo" in the empty state, and the zoom percentage. Those are the next strings to wrap (some are built in ternaries or JS objects).
