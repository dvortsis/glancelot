# SAFE-MODE-01 implementation evidence

Status: implementing the full F064/F057 packet under frozen main3149b6a.
Branch codex/safe-mode-01, assigned worktree/build. This is early component
evidence, not feature acceptance, independent review or a full platform gate.

## Headless command and binding policy

The shipped registry moved unchanged from viewer_controller.cpp into the shared
commandRegistry API. Command initializers (including platform labels and per-task
blocks) and binding helpers are text-equal to the frozen source, except helpers
are inline in a private shared header. command-registry-move.json records the
checked normalized hashes. The controller reuses the same effective-binding
resolution; invalid ordinary saved types still retain their existing fallback.
No new command, scope, key precedence, preference read/write or viewer construction.
Recovery's strict validation of selected source fields is separate and still pending.

The extraction initially exposed an initialization-order regression: a controller-
style lookup before main from another translation unit saw0 commands while a
later call saw66. The committed actual regression log reports6 passed/1 failed,
zero skips. Registry initialization now occurs on first use through a local
static, so that same regression passes. The first attempted rebuild had failed
before testing because its fresh shell lacked MSVC include setup; that compiler
output is retained separately and is not called the red test. The runner now
initializes MSVC on every call.

On Windows Qt6.8.3, the registry component project using the actual
tests/recovery_startup_tests.cpp and src/app/command_registry.cpp passed1/1 CTest
in0.19s; exact Qt cases7 passed,0 failed,0 skipped,11ms. Cases cover pre-main
availability, unique shipped IDs/defaults, user choices overriding an absent new
command's default, Escape/prefix conflict refusal, inactive future binding data
and legacy wrong-type fallback. QCoreApplication-only execution and absent
settings/bak/damaged/sessions files prove this component does not start GUI or
write preferences. Future package unknown-field preservation is not established
by this resolver test alone.

Reproduction project/runner and actual outputs live in safe-mode-01/. The component
build uses build-codex-safe-mode-01/registry-component. This narrow component
project does not replace the required production/test wiring or full feature gate.

## Outstanding complete scope

Raw snapshot/revision/package validation, checked writer and exclusive preimages,
bounded configuration diff/restore, claim-nonce/session guards, dirty/clean atomic
checkpoints, early Unicode/maintenance/safe startup, actual software-RHI frame/RGB
verification and complete failure/cancellation/process-interruption coverage remain
to implement and verify. Enable frozen RecoverySources outside BUILD_TESTING and
RecoveryTests unconditionally with real sources before candidate qualification.
No placeholder pass or partial API constitutes F064/F057 completion. Independent
cross-lane review and full Windows/Docker plus integrated gates remain required.
Physical GPU/DPI/power-loss behavior remains unverified unless measured.
