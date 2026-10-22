# SAFE integration lead decisions,2026-10-21

The user explicitly assigned this session lead/integrator for this round. This overrides the default tool-to-lead routing for this session; implementation ownership and independent cross-lane review rules remain unchanged. Main integration was reserved in b7f080d; cd26e0c imported docs-only INPUT/MONITOR reviews and reconciled queue status. Neither reviewed Claude source branch was changed or merged.

## Source acceptance

The initial0580fe0 review was conditionally favourable on the engine, requiring decisions on preference contention, failed-begin resume and live-Session restore sequencing. All three were corrected, regression-tested and covered by successive actual Sonnet5.5 follow-ups. The03bd326 product correction and796ab411 fixture correction each have retained independent no-blocking acceptance. Main merged cleanly into SAFE in7e8897975d507c29907d6a026792e1ea42b7cedd. safe-source-equality.txt records zero changes to SAFE product/tests/recovery contracts after qualification, plus the exact remaining sync-only files.

The final independent Sonnet5.5 review accepts7e88979 for integration with no required changes. Raw JSON, full unedited report, exact prompt/command and terminal exit are retained here. Session f035406f-cdc5-4062-8450-6f5e4cb01919,44turns,203852ms,canonicalModel claude-sonnet-5-5,exit0. This pass closes the initially uninspected startup-test, process-verifier, package-validator and recovery-wiring areas, relying on the earlier correction reviews for most owner/controller changes. Read-only source acceptance is separate from runtime gate evidence.

## Nonblocking residual decisions

- Preserve documented binding foreign-refresh/fallback edge cases, feedback ownership by string, suppressed terminal sort failure under newer feedback, and loaded terminal-failure coverage gap as follow-ups. These do not reverse the independent no-blocking source acceptance; no required correction is waived.
- Retain the bounded4s teardown and128-channel retry cap. Physical power-loss durability/fsync, sanitizer, physical GPU/DPI and input-to-photon evidence remain unverified, not inferred from process kills or software RHI frames.
- Watcher-budget timing risk remains unresolved. The original180s test and250ms GUI assertions remain intact. The lead will freeze a separate bounded harness follow-up after SAFE integration; no historical timeout is labelled explained or fixed.
- Final review's wrapper note is addressed operationally: the actual integration script records Docker LASTEXITCODE inside the locked block and throws if either full gate fails. No wrapper change is needed for this round.
- The conditional ExportTests include is inherited/inert because its implementation is absent. It does not guard recovery. EXPORT integration must make that include unconditional when its implementation arrives.
- The stale startup-test alternative name, inline Qt spelling uncertainty and failed-preparation environment cleanup are Low follow-ups; the viewer exits before consuming a failed preparation. No unsupported Qt behavior is asserted as verified.
- Process-proof count is descriptive. Completion is verified through every per-step oracle and terminal passed=true/exit0, not the number35 alone. Fresh gate artifacts and their raw reports remain necessary.

## Other lane dispositions

INPUT87b063d remains CHANGES REQUIRED for tree Trash registered routing/shared predicate and parity coverage. MONITORae1b7e4 remains CHANGES REQUIRED for hidden secondary retention, comparison generation fencing, invalid remembered fit and chord dismissal. Preserve core output-specific viewport/color/scale acceptance in MONITOR-OUTPUT-01; advanced presets/display/workspace customization belongs to MONITOR-CUSTOM-01. No silent scope reduction or merge approval is granted to either candidate.

Runtime integration outcome is recorded separately in README.md and gate-status.txt after the two full gates finish. Pending is not passed. The lock remains held until the round is either integrated with evidence or returned with a concrete failure disposition.
