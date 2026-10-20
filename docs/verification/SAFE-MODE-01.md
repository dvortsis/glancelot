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

## Bounded diff reports

Private settings_diff now compares applied current-to-replacement changes and
preserved/unapplied current-to-source differences using component paths. It
distinguishes absence, JSON null, empty strings and empty objects; type-changing
object replacements produce one row rather than duplicating their subtrees.
Paths and rows have stable lexical ordering independent of source insertion order.

Depth64,4096 rows and aggregate3MiB conservative output accounting are enforced
before row allocation/serialization. Accounting includes UTF16/JSON escape costs,
container/row overhead and8KiB for report metadata; repeated long path prefixes
are charged on every emitted row. Inputs are bounded/checked independently,
including unchanged subtrees, so equality cannot hide excessive depth. Limit
failure returns LimitExceeded and a marked bounded partial diagnostic. Cancellation
does not become a usable success. This helper has no RestorePlan constructor.

Actual Windows component CTest passed2/2 in0.71s, recovery78/78 zero skips522ms
and registry7/7 zero skips10ms. New cases cover apply/preserve classification,
absence/null/empty distinctions, unambiguous path components, exact4096 versus4097
rows, depth64 versus65 (including identical over-deep input), repeated long paths
hitting aggregate bytes before row count, single-row object type replacement and
cancellation/generation. See safe-mode-01/diff-* actual outputs and component
project. This is not yet the public immutable preview, JSON CLI serializer or
proof of complete restore authorization. Full feature wiring remains pending.

## Claim identity and held session guards

WindowSlot now records a random per-claim UUID nonce with the existing pid/start
and opening-order slot. A captured WindowClaim owns immutable identity values
and the optional private-root owner, not a WindowSlot pointer. Release revokes
the captured state; a same-process release/reclaim of slot1 cannot reuse the old
capability. Ordinary opening order, next-number behavior and unknown root/live
record fields are retained. Ambiguous process identity is not treated as death;
legacy/unknown-start records remain live blockers rather than recovery authority.

Private ownership guards resolve/check namespace, acquire sessions.json.lock
first, validate the exact live slot/pid/start/nonce and retain that lock for the
caller through its settings transaction. Offline guards authorize no window
claim: they refuse live/ambiguous, malformed/future/oversized or nonregular
bookkeeping and never prune records/reset counters/rewrite sessions. Cancellation
and lock contention return explicit refusal; claims/guards retain temporary-root
lifetime by value. Source ownership does not itself construct a RestorePlan or
perform a settings mutation; final facade/checkpoint integration is still pending.

Actual Windows component CTest passed2/2 in0.95s, recovery94/94 zero skips809ms,
registry7/7 zero skips8ms. New cases cover actual claims and held-lock exclusion,
offline refusal with live records, same-process nonce reuse prevention, two slots
in one process, namespace substitution, changed nonce/start/legacy/unknown-start
records, damaged/future/fractional/invalid/oversized sessions, read-only offline
inspection, Busy/cancellation, nonregular bookkeeping and captured private-root
lifetime. The first compile found a Qt slots-macro variable collision and a test
namespace typo; that failed compile is retained separately. Corrected build and
actual case outputs/project are ownership-* in safe-mode-01/.

Linux/native privilege-denied process variants and the full existing window-slot
suite still need execution. This component does not prove final pre-commit owner
closure, guarded restore/checkpoint ordering, process restart or UI teardown;
those must be exercised in the completed feature, with the held sessions guard
preceding the settings lock and no borrowed controller state.

## Public immutable preview and guarded restore

The public facade now constructs an opaque value-owned plan only after package,
source member, current logical base, selected policy, ownership, bounded diff and
the writer's exact indented output-size checks succeed. Its sf1 fingerprint binds
namespace, raw pair, package, policy, scope, slot and source member. Editing the
public preview fields cannot change the plan. Explicit last-good selection and
corrupt-current recovery disclose loss of reconstructable unknown logical fields.

Apply holds the sessions guard through the shared writer when scoped to a slot,
checks raw revisions before no-change, and revalidates package/policy/captured
claim immediately before replacement. A real exclusive sealed raw preimage is
mandatory before primary replacement; original backup/damaged files remain exact.
Failed or cancelled preimages and later failures retain the diagnostic destination.
Position restores include an explicit restoration checkpoint in the preview,
with an offline clean historical record or the owned claim's dirty identity.

Actual Windows component CTest passed2/2 in4.51s, recovery105/105 zero skips4339ms,
registry7/7 zero skips10ms. Cases include exact sealed preimages, stale plan replay,
no-change, backup-only revision conflict, package changes before apply and after
preimage, invalid/truncated/future previews, immutable plan fields, corrupt current
with explicit last-good source, preimage failure/cancellation, claim revocation
immediately before replacement and oversized merged output from bounded inputs.
Actual outputs/project are restore-* in safe-mode-01/. The two compile corrections
(QStringList append ambiguity and a test aggregate field order) remain recorded.
This is a component gate, not full feature, process or platform qualification.

## Lifecycle checkpoint engine

The actual beginWindow/checkpoint/finishWindow facade now captures the prior own
slot before installing a new claim's dirty marker, preserves the prior payload,
and never resets a repeated same-claim sequence. Navigation stores known position
fields and a new UUID/monotonic sequence in the same shared-writer transaction.
Empty-folder positions remain valid. Finish requires exact claim/revision/sequence
and leaves failed or contended final writes dirty. No-navigation exit may clean
sequence0. Session guards precede settings locks and remain held through commit;
revocation is checked again immediately before replacement. Ordinary checkpoints
use the bounded last-good backup and never create exclusive restore preimages.

Actual Windows component CTest passed2/2 in8.42s, recovery118/118 and registry7/7
with zero skips. Actual checkpoint-* outputs/project include stale sequences and
final acknowledgments, dirty prior-payload capture, no-navigation exit, empty-folder
save, injected write failure, contention/cancellation, release/reclaim and immediate
precommit revocation, future/malformed records, unknown data/other-slot preservation,
and cancellation after commit still acknowledging the persisted UUID. This proves
the engine component; controller worker scheduling, real decode/navigation success
selection and process interruption/restart are not yet qualified.

## Early startup parser component

app/startup_intent provides one option definition table and a non-exiting parser
usable before application construction. It validates maintenance combinations,
required restore fingerprint, selected scope/source/slot, duplicate/unknown/missing
options, incompatible viewer flags and explicit -- image operands. Windows native
argument capture uses GetCommandLineW/CommandLineToArgvW and QString directly,
avoiding narrow argv for Unicode paths. Package operands resolve to absolute paths
before being handed to the explicit-namespace recovery facade.

Actual Windows component CTest passed2/2 in8.19s, startup53/53 zero skips15ms plus
recovery118/118 zero skips. The QCore-only startup executable exercises malformed
combinations, every forbidden maintenance/viewer option, all global scopes,
offline slot/source/fingerprint selection, Unicode/supplementary characters,
explicit -- dash-leading image names and native argument capture. It confirms no
GUI application or settings/session files were constructed. Actual outputs/project
are startup-parser-* in safe-mode-01/. Main still needs to consume this parser;
this component does not prove actual executable Unicode arguments or startup order.

## Headless maintenance adapter

app/settings_maintenance calls the real capture/preview/apply facade with an
explicit context and emits schema1 JSON with stable enum code names and frozen
exit mapping. Diff rows encode component arrays, presence flags, values and
apply/preserve/invalid classification. uint64 generation is emitted as a decimal
string. Restore recomputes the preview and requires an exact current sf1 match;
position/layout maintenance uses offline guards. Serialization includes the
newline in its3MiB bound and reserves final preimage/revision diagnostic space
before mutation. Non-maintenance intent returns ArgumentError/exit2.

Actual Windows component CTest passed2/2 in8.46s, recovery122/122 zero skips8169ms
and startup53/53 zero skips12ms. Real generated-file cases exercise backup/diff/
checked restore, exact preimage, stale fingerprint, no-change, null presence and
unknown preservation, decimal generation, offline live refusal and historical
clean restore, truncated output/cancellation and every stable exit category.
Actual outputs/project are maintenance-* in safe-mode-01/. This QCore-only facade
does not yet prove executable dispatch avoids QGuiApplication; main wiring and
actual CLI process evidence are pending.

## Value-owned controller recovery worker

Private recovery::Session serializes begin, position checkpoints and final clean
marking on a worker. It admits one active operation and replaces only one latest
pending checkpoint. Transient Busy/WriteFailed retries are bounded. Close stops
new accepts and cleans only after the latest accepted sequence actually persisted.
Timeout/failure never reports clean success. Detached work retains immutable claim
and private-root ownership without QObject/controller/startup-stack references;
revocation still prevents replacement after the owner releases its claim.

Actual Windows component CTest passed2/2 in12.50s, recovery126/126 zero skips12284ms
and startup53/53 zero skips17ms. Deterministic gates show100 accepted sequence
requests collapse to checkpoint1 then100 and one clean transaction; stale accepts
and post-close submits refuse. Injected latest-write failure stays dirty. Destroying
both Session and WindowSlot while the worker is blocked retains the private owner
until the revoked transaction returns, leaves original primary unchanged, and then
releases the owner. No-navigation clean and invalid-claim refusal also pass.
Actual outputs/project are session-* in safe-mode-01/. This tests the real worker
and engine, not yet controller decode/navigation integration or whole-app teardown.

## Remaining complete scope

Controller integration of the atomic navigation and lifecycle checkpoints,
early Unicode/maintenance/safe startup, actual software-RHI frame/RGB
verification and complete failure/cancellation/process-interruption coverage remain
to implement and verify. Enable frozen RecoverySources outside BUILD_TESTING and
RecoveryTests unconditionally with real sources before candidate qualification.
No placeholder pass or partial API constitutes F064/F057 completion. Independent
cross-lane review and full Windows/Docker plus integrated gates remain required.
Physical GPU/DPI/power-loss behavior remains unverified unless measured.
