**Disposition: nonblocking. No source or test correction is required before handoff.** This covers only the six-line fixture change at `796ab411`. It is not a full-packet approval or an integration approval, and I didn't rely on the gate passes to judge the fixture.

**Finding 4 was wrong, and I retract it.**
- **Wording:** "no longer covered" and "regression" were incorrect.
- **Before the change:** the two cache tests asserted `isLastDecodeCacheHit()` is true (now `fs_watch_tests.cpp:407` and `:690`). Those assertions fail whenever the 100ms fallback fires. That is the flake itself.
- **No false-hit assertion existed:** neither test ever asserted that the flag is false after a late baseline, so the fixture change removed no coverage.
- **What it did:** it made the tests reliably exercise the cached path they already asserted. So the missing explicit unchanged-source false-hit assertion is a **preexisting additional coverage gap**, not a regression from the six lines.

**No requirement makes that gap blocking.**
- **Late-baseline behavior:** `viewer_controller.cpp:333-334` bumps the generation, cancels, sets `cacheReuseAllowed=false`, then rescans and decodes. That is still covered by `controllerLateCoalescedBaselineDecodesOnce`. Its rows check fresh pixels, `decode:begin` counts of 2 (`fresh-pixels`) and 3 (`newer-open-wins`), and that the newer open wins.
- **Stale results (AGENTS.md):** "stale results cannot replace a newer selection" is covered by the `newer-open-wins` row.
- **Cache identity:** the changed-source miss at `:702` (pixels, hashes and `!isLastDecodeCacheHit()`) is intact. A stale cache hit on changed bytes would fail the pixel check, so the false-hit risk that matters is covered. The only case not covered is an unchanged source plus a late baseline returning a hit, which is a performance property rather than a correctness one.
- **Fixture semantics:** `cacheHooks.baselineDeadlineMs=5000` only lengthens the fallback timer (`viewer_controller.cpp:316`). That path exists only under `GLANCELOT_FS_WATCH_TESTING`, so production keeps its 100ms default. The hooks otherwise keep native registration and the real decoder, and `HookScope` restores the previous pointer. No assertion, timeout or renderer setting changed.
- **Comments:** the added comments at `:391` and `:668` are harmless.

**Optional follow-ups, none required for handoff:**
1. **False-hit assertion:** add a test in a new per-feature file, not `fs_watch_tests.cpp` or root CMake, since root test configuration is lead-owned. It would use a gated `options.read`, as at `:757-761`, to force the late baseline on an unchanged source. It would assert `!isLastDecodeCacheHit()` and a fresh decode. This turns the fallback into deliberate coverage.
2. **Diagnostics:** a `workerStarted` purpose count (decode versus revision) reported on failure. This would make a recurrence of the 5000ms wall-clock bound diagnosable. It is low impact and fails closed.
3. **Budget timeout:** still unresolved and untouched by this fixture. The README now correctly says 180s, and any timeout or setup-cost decision stays with the lead.

I reread the payload, the saved review, both cache tests, `controllerLateCoalescedBaselineDecodesOnce`, `viewer_controller.cpp:300-345` and the proposed patch. I executed nothing and wrote nothing. Hardware, sanitizer and power-loss gaps remain unverified.
