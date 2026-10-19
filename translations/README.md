Translations

Put `glancelot_<language>.ts` files here (for example `glancelot_ar.ts`). When Qt Linguist tools are installed, CMake builds them into
`:/i18n/glancelot_<language>.qm` inside the application, and `app/main.cpp` installs the one matching GLANCELOT_LANGUAGE or the system
language. Generate or refresh a catalog with `lupdate ui src app -ts translations/glancelot_<language>.ts`. Command names are looked up in
the `Commands` context with their English text as the key. Without the tools or a catalog the interface stays English.
