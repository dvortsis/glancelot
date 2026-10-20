Translations

Put `glancelot_<language>.ts` files here. When Qt's `lrelease` can be found (next to the Qt in CMAKE_PREFIX_PATH, or on PATH),
CMake compiles each into `:/i18n/glancelot_<language>.qm` inside the application; `app/main.cpp` installs the catalog named by
GLANCELOT_LANGUAGE (exact file suffix, for example `en_XA`), else the one matching the system locale. Without `lrelease`
nothing changes and the interface stays English.

Refresh a catalog: `lupdate ui src app -locations none -ts translations/glancelot_<language>.ts` (Qt's `qttools`; Linux:
`aqt install-qt linux desktop 6.8.3 linux_gcc_64 --archives qttools`).

`glancelot_en_XA.ts` is a pseudo-locale: every extracted string is accented and bracketed, so a run with GLANCELOT_LANGUAGE=en_XA
shows at a glance which text is not translatable (it stays plain) and where translated text would be clipped. It was generated
from the 209 strings `lupdate` found and re-generates with the same command plus an accenting pass.

Not extracted by `lupdate`: strings looked up at run time by their English text, namely command names (context `Commands`),
controller status messages (`Status`), decoder messages (`Decoder`), grid, layout, theme and delete messages. Add them with
`QT_TRANSLATE_NOOP` or a dedicated source file before a real catalog is started.
