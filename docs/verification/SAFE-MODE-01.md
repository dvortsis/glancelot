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

## Controller checkpoint and lazy-resume integration

The controller attaches the real captured claim to recovery::Session, queues only
successful decoded images or successful directory visits, and captures begin's
previous payload without scanning media. Lazy restore waits for that payload.
Every explicit open/visit (including a failed one) suppresses delayed resume.
Resume rechecks owner lifetime, recovery epoch and navigation/image generations
after announcing status, so a reentrant explicit request or destroyed controller
cannot receive the old restore job. Safe-mode feedback uses the existing status
property and safe mode suppresses resume; no QML or theme token changes.

Shutdown closes only after the newest accepted position persists. Unqueued success,
failed final save or timeout cannot mark clean. Claim replacement asynchronously
retires the outgoing session and holds only the latest next claim, avoiding extra
running workers. Numeric legacy callers retain their existing position-memory path.

Actual Windows component CTest passed3/3 in13.59s: backend127/127 zero skips11855ms,
startup53/53 zero skips12ms and controller10/10 zero skips1509ms. Controller cases
use the actual core/platform and decoder sources with generated PNG/empty folders:
successful empty-folder save/clean, failed explicit visit before a late claim,
explicit visit while begin is contended, successful versus failed decode persistence,
reentrant visit precedence, owner destruction in resume status, safe feedback and
blocked outgoing claim replacement (measured1ms). The first real controller compile
found an earlier registry-adapter toVariantMap type mismatch; removing that needless
conversion fixes it. First test failures were native path separator comparisons;
assertions now normalize separators. Both failure outputs are retained with actual
corrected outputs/project as controller-* in safe-mode-01/.

This component does not qualify main startup dispatch, safe-root/RHI behavior,
process interruption or full legacy/platform regression. Lead build amendment
4745c0e prepares real source lists and the required dedicated controller target;
release and full test wiring still need enablement before any candidate gate.

The unchanged tests/window_slot_tests.cpp was then separately compiled against
the actual controller/recovery sources without GLANCELOT_RECOVERY_TESTING. Its
Windows regression passed31/31, zero skips24360ms; CTest1/1 in24.55s (test24.47s).
This includes existing ordinary settings failure/backup/unknown-key, cross-process
slot coordination, late-claim, successful/failed decode, empty-folder, explicit-open,
destruction-flush and disabled-slot cases. Actual production-flavor component
project and configure/build/CTest/case outputs are legacy-slots-* in safe-mode-01/.
It remains a targeted regression, not the full scripts/build.ps1 -Test gate.

## Pre-application safe environment

app/startup_environment captures an original override/default descriptor before
redirection, resolves default namespaces only after application metadata, and
establishes a QTemporaryDir-owned private root before any application/settings
owner. It refuses preparation after application construction. Windows override
setting uses the wide CRT environment API, matching Qt's lossless wide reader;
narrow UTF8 environment assignment is insufficient outside the ANSI code page
([Qt6.8.3 environment implementation](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/global/qtenvironmentvariables.cpp)).
Process-local setup removes incompatible Quick adaptation/device/debug overrides
and requests D3D11/software on Windows or Mesa software OpenGL on Linux, preserving
the RHI canvas. Request flags are not actual renderer qualification.

Actual Windows startup component passed55/55 zero skips299ms, CTest1/1 in0.46s
(test0.40s). Its real native subprocess calls preparation before QCoreApplication,
passes Unicode/supplementary-character temporary/original directories, verifies
exact redirection, retained/released private-root lifetime, isolated generated
settings writes, byte-identical original primary/backup/damaged/sessions, cleared
inherited adaptation and requested renderer flags. Default descriptor resolution,
invalid temporary parent and late preparation refusal also pass. Actual outputs
and component project are safe-environment-* in safe-mode-01/. No GUI/RHI was
created here; actual viewer dispatch, software device/frame/RGB and Linux remain
unverified. Main still needs this early setup before constructing the viewer.

## Real executable startup, maintenance and software RHI

Main now uses the shared Unicode early parser before application construction.
Help/version/maintenance use QCoreApplication only; invalid arguments return bounded
schema1 ArgumentError JSON before a GUI. Normal viewing attaches the actual claim.
Safe mode prepares its process-owned temporary namespace and renderer request before
QGuiApplication and all settings/session/controller/theme/layout owners. Existing
Qt GUI options such as -platform remain accepted but refuse in maintenance. The
single option table serves both early validation and later viewer option access.

Actual renderer evidence is sampled on the render thread from the window's QRhi,
with bounded device name and actual backend/device type. The local report identifies
whether software classification came from CPU device type or the Mesa renderer
name, and records canvas geometry/zoom/error plus swap and decoded-image timing.
The process verifier samples generated RGB stripes at the actual image center in
the RHI canvas, excluding thumbnails. Flags alone cannot pass this test.

Production release configured with BUILD_TESTING=OFF and built the actual viewer
and decoder. The measured executable SHA256 is
F76DC3E244F31FA4B436B895D11D3870EB15A3B348E29E5DD50FC4F8415659CB.
Initial full and final incremental build/configure outputs, cache proof and hash
are release-* here. Startup component passed59/59 zero skips352ms (CTest1/1 .57s).
Actual scripts/verify-recovery.py then passed33 completed commands on that release:
headless commands run with a deliberately invalid QPA platform and no controller
trace; real Unicode package/image paths and --, checked backup/diff/restore,
stale/no-change/backup-only conflict, tamper, future primary, explicit last-good,
argument/help/version nonmutation, safe and no-resume original byte hashes,
normal preference visibility after safe exit, live-owner offline refusal, process
kill after a successful dirty checkpoint, new-claim lazy restart, historical clean
offline restore and failed explicit-open precedence. Generated media hashes stay
unchanged. Only the owned process is killed; this does not prove physical power loss.

Both safe launches actually used QRhi backend3/D3D11 and CPU device type5,
Microsoft Basic Render Driver. They recorded24 swaps each, first decoded-image
swaps1495ms/1252ms, and canvas samples [255,0,0], [0,255,0], [0,0,255]. The screenshot
is1180x780; exact samples are570/590/610 at y390. Original settings/bak/damaged/
sessions bytes and their full generated namespace tree remain unchanged. All actual
commands/stdout/stderr/traces/reports/screenshots/RGB and interruption evidence are
process-windows/; process-windows-output.txt records the actual verifier result.

Lead amendments188649e/production activationc29b69c and required-test activation
564ecdc are now present: production sources are outside BUILD_TESTING, old provider
consumers attach idempotently, and all four real recovery test targets are enabled
without missing-file/opt-out guards. Full candidate gates and independent review
have not yet run; this runtime milestone is not task completion.

## Remaining complete scope

Full scripts/build.ps1 -Test, Docker/Linux software-RHI and process verification,
independent cross-lane review, any resulting corrections and serial integrated
checks remain required before F064/F057 completion. Re-audit all frozen requirements
against actual full-gate results, including failure/cancellation and other-slot
preservation; component/runtime milestones alone do not qualify the whole feature.
Physical GPU/DPI/power-loss behavior remains unverified unless measured.

## Exact Windows maintenance stdout bytes

The Windows CRT text stream expanded the serializer's explicitly budgeted final
LF to CRLF. Production stdout now switches to binary before writing bounded JSON;
the process verifier requires exact LF output. The old release failed the new
assertion on its first command (process-windows/newline-red). The rebuilt release
SHA256 CE49FBD6740A9633C6956D0E6313E97D097FDF8CEE9C2489D9429C54E40A2B78
passed all33 commands including the byte assertion (process-windows/newline-green).
Its first safe launch recorded28 swaps, first image frame2092ms, observed actual
D3D11 backend3 CPU device5 Microsoft Basic Render Driver and exact RGB samples.

An intervening attempt failed honestly with zero frame swaps, no first image frame
and an invalidated observation, despite previously sampled CPU device details
(process-windows/zero-frame-failure). The cause is unresolved. No renderer flags,
frame requirements or sampling assertions were weakened; the unchanged executable
passed the repeat with no competing viewer/test process present. The failed run
remains part of qualification evidence, not a passing gate.

## First full candidate build attempt

The actual required Docker command stopped at pane_layout_tests linking: its
settings_file.cpp now calls the raw recovery reader, but the lead-frozen consumer
attachment predicate matches only viewer_controller/window_slot providers. Existing
settings-only consumers pane_layout_tests and favorites_tests need attachment too.
The failed actual command output is candidate-docker-first.txt. No Linux CTest or
runtime image pass is claimed. The shared amendment must match settings_file.cpp
under the integration lock; it cannot be applied while Gemini holds that lock for
agent/monitor-output. Existing targets and tests must remain enabled.

The actual full Windows scripts/build.ps1 -Test attempt also stopped at
pane_layout_tests linking with the same four missing raw-reader symbols. Actual
output is candidate-windows-first.txt; Windows CTest did not start. This confirms
the shared consumer attachment omission on both platforms. Dedicated recovery
CMake targets are being checked separately; they cannot substitute for either
required full gate.

Actual dedicated CMake targets linked successfully. CTest returned1: recovery114
passed/13failed/zero skips12075ms; startup59/59 zero skips178ms; controller10/10
zero skips1706ms (CTest3 tests14.41s). The policy failures return Unreadable because
the generated recovery_test_catalog.qrc combines PREFIX /themes with the existing
source alias themes/catalog.json, yielding /themes/themes/catalog.json. Use the
production resource declaration PREFIX / BASE assets for all three recovery
catalogs; the resource alias must remain /themes/catalog.json. Actual per-test
results and generated qrc are candidate-*-first artifacts. These two lead build
corrections remain pending the current Gemini integration lock; no test assertion
or fail-closed production catalog handling should be weakened.

## Actual interruption inside restore transactions

The recovery executable now has a private test-only child mode that uses the
actual public preview/apply facade with existing transaction fault observers.
Four generated-media-independent cases launch an owned child and wait for a
bounded stdout handshake while its real transaction lock is held, then kill only
that child before evidence, after sealed preimage, before primary replacement,
and after primary replacement. All four passed on Windows. They verify exact old
primary before replacement, complete new primary after replacement with unknown
and other-slot fields preserved, unchanged backup/damaged bytes, exact sealed raw
preimages after seal, and successful lock reacquisition after process death.
No production helper mode or hook was added. This does not prove physical power loss.
Actual updated recovery output is restore-interruption-cases.txt:118passed,
13known catalog-path failures, zero skips13561ms; CTest still returns1 (17.18s)
because shared resource wiring remains uncorrected. The new crash cases do not
substitute for the required full Windows/Linux gates.

The same real process-interruption matrix now covers both supported primary and
corrupt primary with an exact supported last-good base. All eight boundary cases
passed (QtTest10/10 including init/cleanup, zero skips1571ms), actual output
restore-interruption-supported-and-corrupt.txt. For corrupt-primary recovery,
pre-commit death keeps exact corrupt primary and last-good bytes; post-commit death
leaves the complete checked replacement based on last-good, retaining its unknown
and other-slot fields plus an exact sealed copy of the original corrupt pair.

The retained zero-swap screenshot visibly contains the generated RGB image; it
still fails the unchanged swap/observed-device gate. Additive local report fields
now record render-callback/invalidation counts and window visibility/exposure,
state, dimensions and bounded screen geometry/DPI metadata to diagnose another
occurrence. The verifier includes those fields in renderer-failure diagnostics.
The actual release rebuild passed (renderer-diagnostics-release-build.txt); runtime
qualification of this diagnostic change remains pending the synchronized full gates.

## Corrected shared wiring and monitor synchronization

Lead amendment f17a2a3 attaches settings_file-only consumers and uses production
resource aliases for all recovery catalogs. The actual regenerated qrc now has
prefix / and alias themes/catalog.json (candidate-catalog-corrected.qrc).
Main's independently integrated monitor-output source is synchronized into the
assigned Codex tree. Conflicts preserve both CMake target sets and startup wiring;
all229 command literal/flag/platform tokens exactly match integrated main, with
its five presentation commands retained in the extracted registry. Actual comparison
output is monitor-registry-sync.txt. Source candidate d4f0e70 begins the corrected
full scripts/build.ps1 -Test and Docker gates. No result is claimed until completion.

## Corrected complete candidate results

Actual source d4f0e70 passed docker build -t glancelot-browser:main .:37/37 tests,
257.43s, then exported runtime config SHA256
f2aba5cc7ee1be0589b5b4f7bce389549a4aa88bab8f090f3885844af3a6180a.
The matching cached build-stage image1983cc0937718cf190b55a5653e8e65685fea557377b8be73b119389e6e182ce
retains actual reports, extracted with a new stopped container removed afterwards.
Actual full command output is candidate-docker-corrected.txt, cache/materialization
proof candidate-linux-evidence-image.txt, and linux-corrected-* reports/CTest log.
Linux recovery135/135 zero skips10844ms, startup59/59 zero skips31ms,
controller10/10 zero skips1131ms. Mandatory process test passed21.04s,33commands.
Raw generated evidence is process-linux/run-azgvnfyf. Its first safe launch used
actual QRhi backend2 Mesa llvmpipe,18 swaps, first image frame817ms, exposed/visible
window, zero scenegraph invalidations and exact RGB red/green/blue canvas samples.

The same source's actual full Windows command completed35/37 in919.59s, with
folder_grid hitting its original400s timeout and fs_watch81passed/1failed/zero skips
(cache-hit assertion noncurrentSameTupleReplacementInvalidatesCache). All remaining
tests passed, including recovery135/135 zero skips13490ms, startup59/59 zero skips
141ms, controller10/10 zero skips1504ms, and process33commands28.06s. Actual first
safe launch used backend3 Microsoft Basic Render Driver CPU device5,35 swaps,
first image frame1150ms, exposed/visible window, zero invalidations and exact RGB.
Full failed command/CTest and per-test results are windows-corrected-* and
candidate-windows-corrected.txt; raw process evidence is
process-windows/corrected-full-run-1k6x8tks. This is a failed complete gate.

After both full runs terminated and no native compiler/CTest process was present,
the unchanged grid binary passed in206.56s under the identical offscreen/software
backend, working directory and original400s timeout, with added file logging:
74passed/0failed/4skipped206358ms. All four existing skips are opt-in review-capture
cases requesting GLANCELOT_REVIEW_CAPTURE; no assertion/backend was changed.
The specific cache-hit case then passed20 consecutive quiet isolated executions
(28.41s total); its last run has3passes including init/cleanup, zero skips.
Actual isolated QtTest/CTest logs are windows-isolated-*. These do not replace a
required complete Windows pass; it must be rerun. The cache-miss cause remains
unproven. Product, test, build and script inputs still exactly match d4f0e70.
