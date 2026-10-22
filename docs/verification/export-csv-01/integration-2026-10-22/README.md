# CSV integration evidence

Gated source845509f1930bd62cb24194a3182d94267c67dafc passed full Windows42/42 in620.01s and fresh Docker42/42 in375.21s. The serial invocation used the main gate-lock script; run-csv-integration.ps1 and complete command output are retained. Final exact merge is5db97272926105e7b6185eca671b5f6a837b9dd4; integrated-proof contains CSV/cache2/2, grid3/3 and production core with tests disabled. Git source/tree equivalence and binary hashes are adjacent.

csv-integration-attempt-1 records the grid assertion failure; attempt-2 records the cache test failure. The latter CTest entry had no per-case Qt output, so the exact failing slot in that original gate is unknown. cache-probe subsequently reproduces cache-hit assertion failures with explicit Qt logs and traces. The reviewed correction isolates verified cache reuse from the designed late-baseline fallback; it changes no production timing or pixel/revision guards.

Raw fixture/review evidence is also in ../..'s neighboring grid-resize-test-01 and cache-baseline-test-01 directories. These raw trees use -text attributes. artifact-manifest.json records byte lengths and SHA256; staged-byte-audit.json verifies raw Git blob bytes against working files. The manifest and audit exclude themselves to avoid recursive hashes. All media were generated/disposable; these results do not establish physical monitor accuracy or native Linux compositor qualification.

The canonical current outcome and remaining work are in docs/handoff/EXPORT-CSV-01-INTEGRATION-2026-10-22.md and LEAD-WRAPUP-2026-10-22.md. Preparation/finalization scripts are retained as historical invocation records, not as a current-state setup command.
