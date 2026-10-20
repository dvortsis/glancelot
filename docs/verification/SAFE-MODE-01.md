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

## Raw settings pair

readRaw now resolves an explicit absolute namespace, acquires the existing
settings.json.lock with bounded/cancellable waiting and reads each raw member
at most1MiB+1 bytes. It distinguishes absent/present-empty, supported/corrupt/
future classifications, rejects nonregular members and retains exact primary
and backup bytes without last-good substitution. sr1 uses the frozen domain,
presence, uint64LE length and SHA256 encoding; ns1 separately binds the resolved
namespace. It never changes primary, backup or damaged evidence. A missing
configuration directory and the ordinary lock file may be created for coordination.

Actual Windows Qt6.8.3 component execution passed2/2 CTest in0.54s, with raw16/16
zero skips246ms and registry7/7 zero skips14ms. The raw cases include an independent
absent-pair wire-vector check, corrupt bytes plus valid backup without substitution,
format/key-order and backup-only revision changes, future/fractional/missing/wrong
schema classification, exact1MiB and oversize limits, directory-member refusal,
equal bytes across different namespaces, relative/invalid requests, precancel,
Busy and cancellation during held-lock contention. Exact outputs and the actual
component CMake project are in safe-mode-01/raw-*.txt.

This is cooperative-writer locking and bounded reads, not exclusion of hostile
noncooperating filesystem changes, network-call cancellation or physical-media
qualification. Linux execution and the complete feature gates are pending.

## Shared checked writer

The private settings_transaction pipeline now owns the existing settings lock,
logical-base selection, bounded serialization and atomic primary replacement.
Ordinary updateSettings adapts its existing void editor into that pipeline;
normal last-good/damaged behavior and SettingsStatus codes remain. Ordinary
parsing still ignores an unused malformed backup when the primary is valid.

Checked requests use the exact raw pair under that same lock and compare both
namespace and revision before calling the editor or returning NoChange. Future
primary schemas refuse writes. Restore policy requires nonempty expected guards
and a preimage writer; preimage failure/invalid success status cannot replace the
primary. Restore leaves .bak/.damaged intact. Failure/cancellation after a preimage
keeps its reported path; cancellation after primary replacement returns Committed.
Private copied fault callbacks are compiled only with GLANCELOT_RECOVERY_TESTING.

Windows component CTest passed2/2 in0.46s: raw/transaction33/33 zero skips318ms,
registry7/7 zero skips8ms. Actual cases cover ordinary unknown-key merge/exact
last-good bytes, corrupt recovery/future refusal, failed backup/oversized edit,
stale backup-only revision before no-op, no-change evidence integrity, missing
restore guards, failed/invalid preimage status, injected failure and cancellation
at four pre-commit stages, and cancellation after commit. See transaction-* logs.

Preimage callbacks in these pipeline tests use disposable generated files to
verify ordering and failure behavior. The real bounded sealed package writer,
package revalidation and public apply facade are not implemented or qualified by
those callbacks. Full old settings/window/controller suites, Linux, feature
build wiring and independent review remain required. This is component evidence.

## Sealed raw backup packages

captureBackup now captures a consistent raw pair under the existing lock, releases
that lock and writes into an exclusively created new destination. Only fixed
settings.json/settings.json.bak members are written with NewOnly; a staged manifest
is flushed and renamed to manifest.json last. Cancel/failure before seal leaves
an inert partial directory, never an applicable package. Cancel after publication
reports Ok. Captured corrupt/future bytes remain diagnostic, not an applicable
restore source selected by silent substitution.

Private package validation reads at most8KiB manifest and1MiB per member, checks
schema/seal, exactly two ordered fixed-name descriptors, presence/length/lowercase
hashes and actual bytes, rejects unexpected/undeclared/nonregular members, and
uses bounded root-only enumeration. sp1 binds exact manifest and member hashes
using the frozen encoding. It does not execute paths or recursively inspect data.
Destination collisions never overwrite the existing package. Source originals
remain unchanged; destination work does not retain the settings lock.

Windows component CTest passed2/2 in0.84s; raw/transaction/package53/53 zero skips
620ms, registry7/7 zero skips12ms. Actual tests cover corrupt/future exact bytes,
absent/empty capture, revision stability, existing-destination collision, modified/
missing/unexpected/undeclared members, unsealed/future/arbitrary-path/duplicate-name/
fractional-length/hash/oversized-manifest refusals, cancellation at five stages,
pre-seal failure, and another cooperating writer acquiring the source lock and
changing settings during destination work while the captured package retains the
old bytes. Exact outputs and component project are package-* in safe-mode-01/.

The package writer can be reused for real transaction preimages, but public apply,
source-selection policy and plan/package revalidation are still pending. These
tests do not establish physical power-loss durability, every filesystem alias/
permission variant, exclusion of hostile noncooperating writes, Linux behavior
or the complete SAFE-MODE gate.

## Known-field restore policy

Private settings_policy now merges the full frozen global sections (viewing,
browsing, appearance, bindings, performance) or one explicit position/layout
slot. Missing selected source keys remove only owned keys to use ordinary
defaults. Unknown current nested fields, future binding IDs, unknown panes,
other slots and recovery/session state survive. Unknown source values are not
made authoritative. Invalid selected known values reject the whole replacement;
unrelated malformed sections remain untouched. An incompatible selected current
container refuses rather than silently discarding it. Future selected layout
versions are never downgraded.

Validation uses the actual shipped command/binding logic and bundled catalog
through a QCore-only component; no viewer/theme/layout construction or saved-path
scan. Pane defaults are extracted unchanged into a shared private data helper;
layout-defaults-move.json records an exact normalized-body comparison to frozen
3149b6a. The policy revision binds a versioned domain, shipped command/default
semantics, catalog and pane data. Changing supported field/range semantics must
also bump its policy version.

Windows component CTest passed2/2 in0.77s: recovery68/68 zero skips485ms and
registry7/7 zero skips9ms. New cases cover complete global merge with unknown
nested data/per-slot isolation, missing selected known keys, nine invalid-value
rows, unrelated malformed section retention, strict bindings including Escape/
prefix conflicts, selected position paths/unknown fields, known layout pane
updates with unknown panes/other future slots preserved, and policy revision
stability. Actual configure/build/cases/project outputs are policy-*.

This helper is a schema merge, not an ownership capability or a public restore
plan. It does not yet produce the bounded preview or authorize slot writes.
Those remaining checks must precede real transaction application. No product
layout, geometry, theme tokens, command precedence or QML behavior changed.

## Remaining complete scope

Real restore-preimage integration and public checked restore,
bounded configuration diff/preview, claim-nonce/session guards, dirty/clean atomic
checkpoints, early Unicode/maintenance/safe startup, actual software-RHI frame/RGB
verification and complete failure/cancellation/process-interruption coverage remain
to implement and verify. Enable frozen RecoverySources outside BUILD_TESTING and
RecoveryTests unconditionally with real sources before candidate qualification.
No placeholder pass or partial API constitutes F064/F057 completion. Independent
cross-lane review and full Windows/Docker plus integrated gates remain required.
Physical GPU/DPI/power-loss behavior remains unverified unless measured.
