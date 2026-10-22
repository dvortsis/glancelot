**ACCEPT.** The narrowed routing at `83100c4` closes the mandatory fixes from `CLAUDE-ROUTING-REVIEW.md`. I edited no files and ran no builds or tests. This is not a gate pass, and I'm relying on your two logged test runs for the test results.

**Skills read:** I read all nine `SKILL.md` files, in full:
- application-interface-design
- layout-grids
- layout-spacing
- typography-ui
- component-geometry
- iconography-ui
- motion-ui
- responsive-layout
- design-review

Key routing falls under application-interface-design. Its rules are complete keyboard operation, no keyboard traps, functional correctness over defaults, and preserving the existing product's pane behavior. The patch changes no layout, spacing, type, geometry, icons or motion, so the other eight skills raise nothing.

**Source, `ui/Main.qml:410-423`:**
- **Narrowing:** the `paneCommand` predicate is now `scopeOf(id) === "grid" || "tree" || (printable && (chordPending || commandInfo[id].steps.length === 2))`.
  - Grid and tree scoped commands keep the old behavior.
  - A global printable key overrides type-to-search only as a registered chord starter or as the second step of a pending chord.
  - A single-step global key such as `G`, `S` or `Shift+S` fails the predicate, so `PhotoGrid.qml:514` still accepts the ShortcutOverride and type-to-search gets the key. That fixes the regression.
- **Printable range:** `printable` is still 0x21–0x7e with no Ctrl, Alt or Meta, so Space and native navigation keys are untouched.
- **Safe lookup:** `commandInfo[id]` is only read after `id.length > 0`, and `resolveKey` only returns ids that exist in `commandInfo`, so there's no undefined access.
- **Pending chords:** the `chordPending` branch is unchanged apart from the shared predicate. Escape, `FolderTree.qml:530` and `PhotoGrid.qml:503-519` are untouched.

**Test, `tests/input_ui_tests.cpp:352-378`:**
- **Coverage:** with the default map and the grid focused, `G`, `S` and `Shift+S` produce `searchQuery == "gss"`. `gridMode` stays on and sort key and direction are unchanged. This is the case the previous review said was missing.
- **Chord path:** after Escape clears the query, a `K, S` binding arms (`chordPending`), the second step toggles `sortDescending`, and `searchQuery` stays empty throughout.
- **Placement:** it sits in the existing per-feature `input_ui_tests.cpp`, not `grid_ui_tests.cpp`.
- **Logged runs:**
  - `printable-original-failed.txt`: against the broad routing it fails at `gridMode` (0 vs 1), with 2 pass and 1 fail.
  - `printable-corrected.txt`: against the narrowed routing, 5 pass and 0 fail, including the Escape and native-remap tests.

  The log lines up with the source: the `G` press is what fails under the broad version.

**Optional item:** the existing-query policy is unchanged. While a query is active, a two-step chord starter like `K` can still interrupt it. It's documented and not required.

**Non-blocking:** the logs show `ShortcutCheatSheet.qml:327 keys is not defined` and `PhotoGrid.qml:314 Unable to assign [undefined] to int` warnings. They also appear in the passing tests and are outside this patch.

This does not cover the full `scripts/build.ps1 -Test` and Docker gates you're starting separately.
