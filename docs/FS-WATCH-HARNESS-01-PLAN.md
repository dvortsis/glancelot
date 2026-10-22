# FS-WATCH-HARNESS-01: bounded phase observability

Lead-frozen October21 after SAFE/startup integration. This implements the deferred follow-up from docs/verification/safe-mode-01/lead-integration-2026-10-21/LEAD-DECISIONS.md and budget-phase-diagnostic/README.md. User assigned Codex lead and authorized continuing unblocked Codex work.

Owner Codex; branch codex/fs-watch-harness-01; worktree .worktrees/codex-fs-watch-harness-01; build build-codex-fs-watch-harness-01. Only active Codex implementation. Independent actual Claude Sonnet5.5 review required; no self approval.

## Decision and boundaries

Historical timeout phase remains unproven. The retained successful diagnostic shows fixture generation/removal dominates, but it cannot justify moving work outside the existing180s whole-process bound or changing real filesystem workload. Keep fixture lifecycle and every deadline exactly as-is. First make future failed-phase evidence permanent. This is an observability correction, not a timeout fix or guarantee of timing on arbitrary hosts; that risk stays open after this packet. No setup/cleanup exemption, smaller fixture or native watcher substitute.

Implementation may edit only tests/fs_watch_budget_tests.cpp and task evidence/docs in its worktree. Add bounded phase-name/elapsed logging for baseline/setup/initial publication, ordinary writer/recovery, active-filter writer/recovery, removal/rename and destruction. Total-lifetime guard must outlive all method fixtures so ordinary assertion failures and fixture destruction are included. Fixed progress lines every25000 generated/removed rows may identify an interrupted I/O loop; no per-row/unbounded logging, no file paths/content. Log outside measured GUI intervals so it does not add synchronous output to the250ms measurements. Keep original final publication reports.

No production files, shared APIs, QML, CMake, workload size/content, selected identity, writer cadence/durations, filter semantics, assertions, skips, timeouts, decoder or renderer changes. All99999 real writes and removals remain synchronous in their existing order; all100000 rows, original complete-baseline/rename retention, native identity, last replacement metadata,250ms GUI/publication and180s process limit remain mandatory. Loop braces for adding progress are permitted; body operations remain identical.

## Validation and integration

Review exact baseline-to-candidate diff, preservation of all assertions/workload and RAII ordering. Retain a source-equivalence audit that removes only added logging/guards/braces and compares remaining code, or provide an equally concrete diff audit. No test of a logging helper that mirrors implementation is required. Run real targeted fs_watch_budget and inspect bounded phase ordering/count and lifetime output. Actual Sonnet reviews cross-lane source/diff; full Windows scripts/build.ps1 -Test and docker build through main gate-lock remain required before done, preserving actual outputs/failures. Synchronize main and serially integrate under integration_lock with independent confirmation if source changes, exact integrated-tree comparison and a targeted integrated check. Do not overlap the user's live test-app gate; wait through gate-lock when full gates begin.
