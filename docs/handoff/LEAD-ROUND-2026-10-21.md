# Lead round outcome,2026-10-21

User assigned Codex as lead/integrator this round. Queue and review routing are reconciled on main. **SAFE was not merged. STARTUP-WINDOW-01 is ready for Codex implementation.**

## SAFE integration result

Reviewed SAFE synchronized cleanly with main at7e889797. Actual final independent Sonnet5.5 source review accepted it with no required changes, preserving all stated limits. Complete gates ran serially through the main machine gate-lock: Windows36/37,601.99s,exit1; Docker37/37,283.69s,exit0. Windows failed the mandatory actual software-RHI image-frame proof. An identical isolated oracle failed again. No assertion, renderer or deadline was changed and no failed run was waived.

Additive diagnostic logging pinned the failure to an offscreen native startup rectangle: the failing window was created at(0,1600),2360x1560, wholly outside the two reported screens. It got no expose and no swapped image frame. In the same diagnostic, the preceding safe window was created onscreen at1180x780 and passed22swaps. The earlier Qt/OS placement decision remains unproven; desktop lock state is unknown. Screenshot-only rendering is not substituted for live swaps.

[Full outcome and actual outputs](../verification/safe-mode-01/lead-integration-2026-10-21/README.md) retain both full gates, fresh Linux35-command proof, failed Windows runs, independent reviews and bounded child Qt environment/timeline evidence. All531 raw runtime artifact files were checked byte-identical to staged Git blobs on both the SAFE worktree and main. Original product/test source remains unchanged from synchronized7e88979; source evidence commit1b7c3db was imported docs-only as e41956f.

## Ready Codex packet

[STARTUP-WINDOW-01 plan](../STARTUP-WINDOW-01-PLAN.md), private value API src/app/startup_window_geometry.hpp and dedicated cmake/StartupWindowGeometryTests.cmake are lead-frozen. Actual Sonnet5.5 contract review required outer-frame/client-coordinate and degenerate-input clarifications; f9c8adc resolved both and its confirmation ACCEPTED with no required amendments. This accepts the contract only, not an implementation.

Assigned branch codex/startup-window-01, worktree .worktrees/codex-startup-window-01, build build-codex-startup-window-01. Base is the reviewed SAFE source plus documentation evidence, merged with this main contract. SAFE waits in review while this is the sole next Codex implementation. The packet must preserve valid placements, measured native frame/RGB proof, minimum controls, renderer policy and every existing timeout. It requires independent actual Sonnet review and fresh full Windows/Docker gates with its new target mandatory before combined integration. Lead removes the temporary implementation-presence guard when landing it.

Watcher harness timing follow-up remains deferred behind this blocker. No lane implementation was performed in the lead role.

## Other lanes and locks

INPUT87b063d and MONITORae1b7e4 remain CHANGES REQUIRED; their exact reports and raw outputs were imported docs-only to main in cd26e0c. Claude owns the fixes and supplies new pinned candidates. Core monitor viewport/color/scale acceptance is retained; advanced customization remains MONITOR-CUSTOM-01. Neither source branch was changed or merged.

The lead integration lock is released in the closing commit. No SAFE/INPUT/MONITOR packet is marked done. Hardware GPU/DPI accuracy, input-to-photon, sanitizer and power-loss evidence remains unverified. Automatic read-only reviews are authorized by the user.
