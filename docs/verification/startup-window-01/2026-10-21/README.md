# STARTUP-WINDOW-01 and SAFE combined qualification

Original SAFE synchronized source7e889797 was independently accepted but Windows failed36/37 at recovery_process. Repeated native diagnostics proved a wholly offscreen window, no exposure and no actual swaps; screenshot-only rendering was not accepted. See docs/verification/safe-mode-01/lead-integration-2026-10-21/README.md for unaltered old failures.

## Source and review

Startup branch codex/startup-window-01 is stacked on reviewed SAFE plus frozen90af854 contract. Source92df42e adds allocation-free bounded outer-frame geometry helper, mandatory dedicated tests and narrow app adapter with pre-grab window/RHI/frame snapshots.411f8c6 preserves pending fullscreen/maximized states. Current main INPUT/monitor integrations were synchronized96ce989/6df03c7, then compatibility tests ca907d1 and corrected testsetup db62544. Candidate db62544 is the pinned accepted full-gate source.

Lead amendment docs/SAFE-STARTUP-MAIN-SYNC-2026-10-21.md freezes the necessary compatibility: current-main exact command table moved into SAFE sharedregistry; scope/chord/hold validation shared GUI/headless; original asyncbindingrollback preserved; value-owned per-modifier retries and selected viewingrestore semantics. Startup geometry header/public APIs, QML, renderer, decoder, verifier/timeouts/oracles are unchanged. command-table-equivalence.json records exact table equality.

Actual independent Sonnet5.5:
- Initial reviewattempt on92df42e returned servicequota, exit1; no approval.
- Combined95d7ea97-5bf9-414b-8929-12fa9c4334ae accepted helper/adapter/pregrab after reading required native UI skills, but only partly checked compatibilitytests.
- Followup85e99c74-00ae-4f8a-85a0-6886fb3ab7dc directly read compatibility implementations/tests and ACCEPTED whole compatibility. Raw JSON, stderr/status and extracted unedited results retained. Neither review claims runtime/fullgate approval.

## Focused/native probes

Initial92df42e targetedbuild passed. Geometry18/18, zero skips6ms. Unchanged35-command verifier passed31.53seconds on Windows (run-cnw8g4uw). Safe26/27 each recorded23pregrabswaps and exposure, recovered frame from(0,1569,2360,1591) to(100,0,2360,1392). Normalruns also recovered. This earlier source is not finalcombined qualification.

On db62544, focused CTest6/6 passed161.23seconds: geometry,window_slots,input_ui,recovery,recovery_controller,recovery_process. Recovery139passed0skip,controller30passed0skip. Native35-command process39.41seconds, run-60s9y01c. The initially authored schema-only import test was corrected before execution because ordinaryimport merges and cannot itself delete absentmodifierfields; the correctedtest uses actual recovery selectedViewing apply then reload.

## Full gate commands

Run from candidate worktree through main lock, one PowerShell process. Actual wrapper full-gates.ps1 retains Windows exceptions and explicit native Docker exit codes:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/startup-window-01 -Run {
    & ./scripts/build.ps1 -BuildDir build-codex-startup-window-01 -Test
    docker build --progress=plain -t glancelot-browser:main .
}
```

Fresh Windows39/39 passed670.61seconds,exit0. Mandatorygeometry target present. Recovery_process30.51seconds passed35commands in run-7s8dvcy0. Before screenshot bothsafeimage runs show exposure/softwaredevice and21/22actualswaps. Their placement was alreadyvisible and remained Unchanged; do not claim this fullrun re-created offscreen failure. Earliernative probes supply recoveredplacement evidence separately. windows-full-native-audit.json checks result and pregrab fields.

The following completion section supersedes pending status at the initial evidence stage; actual exit-status files remain authoritative.

All generated media/settings are disposable. Hardware color accuracy/HDR/real inputlatency/powerloss and sanitizer coverage are unverified. Geometry arithmetic and attachednative exposure evidence do not prove every platform/windowmanager behavior.


## Completed Linux and integration checks

Fresh Linux full gate39/39,340.27seconds,exit0; recovery_process21.05seconds and35commands passed in run-smv7l0fv. Recovery139/controller30/startup61 cases all pass with zero skips. Geometry target appears in full Windows and Linux gates. Both safeLinux reports have18actualswaps, exposure and softwareRHI observed before grab. The build-stage export returns0 and explicitly reuses cached compile/CTest layers; it is evidence export, not a second fresh Linux gate. Temporary export container was removed.

Integrated main `e39eee20f750fbf6ab5fa2e4fc4e56e0114169ae` is a no-ff merge of accepted db62544. `git diff --exit-code db62544 e39eee2` returns0: the whole integrated tree equals the reviewed/full-gated candidate. Checks on that exact commit pass5/5,67.03seconds,exit0 (startup_window_geometry,recovery,recovery_startup,recovery_controller,recovery_process). This is focused integrated qualification plus exact tree equality, not a claim of a second fullsuite at the mergecommit. Native integrated35-command run-223ms761 passes; safe26/27 pregrabactualswaps24/22, exposedtrue, softwareRHIobserved. No product source changed during evidence closeout.

Actual integrated command (project-local cmake/Qt bins prepended to PATH):

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/startup-window-01 -Run {
    ctest --test-dir build-codex-startup-window-01 -R '^(startup_window_geometry|recovery|recovery_startup|recovery_controller|recovery_process)$' --output-on-failure
}
```

Raw complete outputs/status, actual wrappers, prior quota failure, both independent accepted reports, Windows four generated native runs and Linux generated run are retained here. `all-native-proof-audit.json` validates35commands andsafe pregrab swap/exposure/softwaredevice for each. Initial source92df42e is run-cnw8g4uw; final focuseddb62544 is run-60s9y01c; fullWindowsdb62544 is run-7s8dvcy0; exactintegratede39eee2 is run-223ms761. No failed historical SAFE evidence was removed. Original settings/media are protected by the unchanged verifier's byte checks. Raw bytes are preserved with binary attributes.
