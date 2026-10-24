Translations

Put `glancelot_<language>.ts` files here. When Qt's `lrelease` can be found (next to the Qt in CMAKE_PREFIX_PATH, or on PATH),
CMake compiles each into `:/i18n/glancelot_<language>.qm` inside the application; `app/main.cpp` installs the catalog named by
GLANCELOT_LANGUAGE (exact file suffix, for example `en_XA`), else the one matching the system locale. Without `lrelease`
nothing changes and the interface stays English.

Refresh a catalog: `lupdate ui src app -locations none -ts translations/glancelot_<language>.ts` (Qt's `qttools`; Linux:
`aqt install-qt linux desktop 6.8.3 linux_gcc_64 --archives qttools`).

`glancelot_en_XA.ts` is a pseudo-locale: every extracted string is accented and bracketed, so a run with GLANCELOT_LANGUAGE=en_XA
shows at a glance which text is not translatable (it stays plain) and where translated text would be clipped. Regenerate it with
`lupdate ui src app -locations none -no-obsolete -ts translations/glancelot_en_XA.ts` followed by
`python3 translations/pseudolocalize.py translations/glancelot_en_XA.ts`, which fills every unfinished message and keeps
placeholders (%1, %L1, %n). STACK-F054-COVERAGE part 2 regenerated it with 582 messages. `translation_coverage_ui_tests` reads this
file directly and fails when a named surface shows plain English or a message is unfinished.

Not extracted by `lupdate`: strings looked up at run time by their English text, namely command names (context `Commands`),
controller status messages (`Status`), decoder messages (`Decoder`), grid, layout, theme and delete messages. Add them with
`QT_TRANSLATE_NOOP` or a dedicated source file before a real catalog is started.
