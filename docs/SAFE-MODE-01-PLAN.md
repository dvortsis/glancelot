# SAFE-MODE-01 frozen contract

Lead freeze, October20,2026, after FS-WATCH's reviewed source/candidate handoff.
FS-WATCH remains in integration until its pinned merged checks finish. This is
the next Codex implementation: branch `codex/safe-mode-01`, worktree
`.worktrees/codex-safe-mode-01`, build `build-codex-safe-mode-01`. Action
`act_mvfhyfit_af2798dd4e13`. Preserve REQUIREMENTS SYS-03/UX-12 and the complete
F064 safe-mode/diff/backup/restore plus F057 crash-recovery engine. Claude owns
the later recovery UI. Do not merge this packet while another integration lock
is held. If FS needs another implementation correction, serialize Codex work.

## Shared interfaces and permitted source changes

Frozen headers: window_claim.hpp, settings_recovery.hpp, recovery_checkpoint.hpp,
command_registry.hpp; additive WindowSlot::claimIdentity and controller
setWindowClaim/setSafeMode. Existing SettingsStatus values and ordinary writer
callers stay compatible. Numeric setWindowSlot keeps legacy position memory for
existing callers/tests but cannot authorize recovery metadata/restore/clean marks.
The application attaches its actual claim. No external plugin ABI is promised.

Implement private settings_recovery.cpp, recovery_checkpoint.cpp, a checked writer
pipeline shared with settings_file.cpp, and command_registry.cpp. Move the shipped
registry and pure binding resolution out of the controller without changing IDs,
labels, defaults, scopes, hold/repeat or precedence. Read the same bundled theme
catalog without ThemeManager construction. A private shared layout-schema/default
helper may extract existing PaneLayout data unchanged; no layout/geometry change.
Controller/app startup adaptations and required feature tests are assigned here.
No product QML edit, new dispatcher, automatic catalog, cloud, telemetry, update,
publishing or original-media write. Viewing remains read-only; existing explicit
file-operation user intent remains owned by its packet. Use native UI skills if
implementation reaches visible UI behavior; preserve tokens/branding.

## One writer and immutable values

All facade filesystem operations run on a worker or a headless maintenance path.
An interactive adapter owns values, one active/one newest pending request, stop
tokens, generation checks and guarded completion; no callback can borrow a dead
controller or mutable preview. A blocked OS call cannot hold UI teardown forever.
Options lock timeout is validated in[0,4000]ms. Do not hold the settings lock
recursively or expose a callback that can disable a required restore preimage.

Context names an explicit absolute configuration directory. Resolve/normalize it
once and bind plans to the resulting namespace; do not re-read redirected env at
commit. Cooperating processes are serialized by the existing OS-held FileLock.
Context and the lifetime-bearing WindowSlot constructor retain an optional shared
temporary-root owner. Immutable plans and claim values retain that ownership too;
an abandoned worker cannot outlive its private namespace or recreate a dead root.
Do not unlink lock files. Native reads/hashes cannot exclude a hostile writer
outside that protocol; no stronger guarantee is claimed.

Read raw primary and optional .bak before last-good substitution under the writer
lock. Presence differs from present-empty. Each member is at most1MiB. A failed
or oversized read yields no usable snapshot/revision. Raw read/capture can report
supported/corrupt/future bytes without silently treating them as empty defaults.

Raw revision sr1 is SHA256 over ASCII `Glancelot.SettingsPair`, zero byte, version
byte1, then primary and backup in that order. Each contributes presence byte0/1,
uint64 little-endian length,32 raw SHA256 bytes; absent has zero length/zero hash,
present-empty has SHA256(empty). Prefix the lowercase hex result with `sr1:`.
Namespace identity ns1 hashes domain `Glancelot.SettingsNamespace`, zero/version1,
uint64LE length plus UTF8 resolved normalized directory; prefix `ns1:`. Use native
case semantics consistently; a namespace hash alone is not an ownership proof.

Restore checks raw primary AND backup revisions under the same writer lock before
even returning NoChange. Whitespace/key-order/backup-only edits invalidate old
plans. Future primary schema is Unsupported even with a valid last-good copy.
Corrupt primary with valid supported backup may use that logical base, disclosing
that corrupt bytes cannot reconstruct unknown logical fields. Without any valid
base, return Corrupt without mutation; never implicitly invoke reset-aside.
Explicit reset remains a separate operation preserving its original file.

## Backup package and checked restore

An exclusive new directory contains `manifest.json`, optional `settings.json`
and optional `settings.json.bak`; no recursive enumeration or arbitrary member
paths. A supported package has a sealed manifest published last, at most8KiB,
schema1, exactly two ordered member descriptors (primary/backup), each with fixed
name, presence, byte count and lowercase SHA256 (absent length0/zero hash).
Capture retains exact raw bytes, including corrupt/future diagnostic input. Never
overwrite an existing destination. Fixed member writes are exclusive; an inert
partial package is not applicable. Capture holds the settings lock only while
reading the consistent raw pair, not while writing the new destination.

Validate fixed names, descriptor types, presence, lengths, hashes, schema, regular
members and seal. Reject duplicate/arbitrary/symlink member references or missing
declared members. Enumerate at most4 root entries to reject unexpected package
files without recursion. Each member1MiB, manifest8KiB, aggregate package3MiB.
Selected source is explicitly Primary or LastGood; never silently substitute a
corrupt/future/missing selected member. LastGood selection may use a supported
backup in an otherwise diagnostic capture after integrity checks.

Package revision sp1 hashes domain `Glancelot.SettingsPackage`, zero/version1,
uint64LE manifest byte length and32 raw manifest SHA256 bytes, followed by the
same ordered raw-member presence/length/hash encoding. Prefix `sp1:`. Reordering
or formatting the manifest changes its revision. The manifest contains no hash
of itself. Package/schema failure cannot yield an applicable plan.

Preview owns validated source bytes and an opaque immutable RestorePlan. Diff
paths are components, distinguish absence/null/empty and show apply/preserve/invalid
treatment. Bound depth64, rows4096 and aggregate encoded preview3MiB, including
path/value/structural cost with conservative UTF16/container accounting. Do not
repeat whole ancestor subtrees in descendant rows. Limit exhaustion produces a
bounded partial diagnostic and NO plan; never apply a partial diff.

Preview fingerprint sf1 hashes domain `Glancelot.SettingsPreview`, zero/version1,
then length-delimited UTF8 namespace ID, policy ID, current raw revision, package
revision, typed scope, decimal slot (empty for global) and selected member, in
that order, each length uint64LE. Prefix `sf1:`. Scope strings are global/viewing/
browsing/appearance/bindings/performance/owned-position/owned-layout/offline-position/
offline-layout; member strings primary/last-good. Policy ID is versioned and must
change with supported fields/registry/default semantics. A fingerprint prevents
stale intent, not deliberate same-user forgery/authentication. Interactive APIs
carry the opaque plan; CLI re-previews and compares the expected fingerprint.

Apply validates namespace/package/policy/current raw pair and ownership before
any mutation. Seal a uniquely named exclusive exact raw preimage under the
configuration recovery-preimages directory before primary replacement; seal
failure aborts. Keep preimage path in the result after later pre-commit failure.
Apply replaces primary atomically through the shared writer; leave existing .bak
and .damaged untouched (the preimage retains both original raw members). NoChange,
Conflict and pre-commit Cancelled do not write primary/bak/damaged. Once primary
replacement succeeds, return Committed even if cancellation races completion.
Capture reports Ok after its own successful seal, not Cancelled after publication.
NoChange is not an error/partial preview. LimitExceeded is distinct from unreadable
or malformed input. No automatic deletion of backup/preimage evidence.

## Known-field policy

Global All contains Viewing, Browsing, Appearance, Bindings, Performance only. It
excludes windows/layouts/recovery/sessions. Missing selected known source fields
remove only those owned keys to obtain actual ordinary defaults. Preserve unknown
current fields, nested objects and command IDs. Unknown source fields are shown
unapplied, not made authoritative. Invalid selected known values reject the whole
plan; unrelated malformed current sections stay untouched.

- Viewing: boundary continue/stop/loop; wheel navigate/zoom/pan.
- Browsing: filmstrip/tree/treePinned booleans; sort.key name/modified/size/shuffle,
  descending boolean and integer seed0..2^53-1 (zero is the ordinary default).
- Appearance: textScale[0.75,2], reducedMotion boolean, appearance.family from the
  shipped catalog and mode system/dark/light. Preserve other appearance keys.
- Bindings: only shipped command IDs; strings parsed as portable sequences with
  existing prefix-conflict/reserved-Escape and user-choice-over-new-default rules.
  Unknown IDs/values remain stored but are not parsed as active commands.
- Performance: root storageProfile auto/ssd/hdd/rotational/network, preserving
  accepted legacy hdd semantics. A performance object is otherwise unknown.
- Position: only windows.<selected-slot>.folder/image/saved. Paths empty or absolute,
  no embedded NUL, at most32768 UTF16 units each; saved is a nonnegative exact
  integer at most2^53-1. Validate without scanning media. Preserve nested unknowns.
- Layout: only the selected supported version1 slot, locked, center orientation
  horizontal/vertical, ratio[0.1,0.9], mode preview/grid/combined; known shipped
  panes' region left/right/top/bottom/center, integer order[0,99], extent[32,4096],
  visible/frozen booleans. Preserve unknown panes/nested values/other slots.
  A future selected layout version refuses restore instead of downgrading it.

## Ownership and recovery checkpoints

Each new WindowSlot claim records a random canonical UUID nonce in schema1 live
entries, plus existing slot/pid/start. Preserve opening order, unknown fields and
legacy records. Captured WindowClaim owns immutable values, not a WindowSlot
pointer. Validate exact resolved namespace, slot, pid, precise start and nonce
under sessions lock held through settings commit. Numeric slots or process-start
sentinel1 cannot authorize recovery; release/reclaim in the same process invalidates
old tokens. Session guard always precedes settings lock. Ambiguous process state
is not proven dead. Offline per-slot restore refuses while any live/ambiguous
record exists or bookkeeping is damaged, unreadable, oversized or future-versioned.
Global operations need no slot claim and cannot modify per-slot data.

Recovery namespace: `recovery.version=1`, `recovery.windows.<slot>` with version1,
claimNonce UUID, pid, start as decimal string, state dirty/clean, sequence as a
uint64 decimal string, checkpointRevision UUID (or empty with no position), and
origin navigation/restore/empty. Preserve unknown data and future recovery versions
without downgrade. New beginWindow atomically captures the previous own-slot
position/dirty state before installing the new claim's dirty record, sequence0,
retaining the last checkpoint payload. No eager saved-path/media enumeration.
An offline-restored historical record has empty claimNonce, pid0, start"0",
sequence"0", state clean and origin restore. It is not a live ownership token;
the next legitimate begin installs its own identity while retaining the payload.

Checkpoint merges only known position keys and dirty checkpoint reference in one
ordinary atomic writer transaction; new positive sequence must exceed the current
same-claim sequence. Failed decode/navigation never replaces a successful checkpoint.
Preserve existing successful empty-folder position behavior. Ordinary checkpoints
use the existing bounded last-good backup, not an unbounded exclusive preimage on
every navigation. Explicit position restore is intentional checkpoint restoration,
origin restore, with a fresh revision; offline restore has no live claim identity
and marks that completed maintenance transaction clean, without scanning media.

finishWindow marks clean only when newest accepted position persistence succeeded
and expected claim/revision/sequence still match. Busy/write failure stays dirty.
No-navigation exit may clean sequence0 while retaining its previous checkpoint.
Controller serializes one active/one latest pending value-owned checkpoint and
bounded retries. Old completions cannot overwrite newer state or touch a dead
owner. Explicit open/navigation wins over delayed lazy restore, including late
claim and failed explicit open. Layout loading retains existing per-slot behavior.

## Startup and maintenance commands

Use one option-definition/parse source before and after application construction,
including Unicode, missing/unknown options and `--`. Windows obtains original
Unicode arguments before QGuiApplication; do not round-trip paths through narrow
argv. Capture original config override/default descriptor before redirection;
resolve the namespaced default after Glancelot application metadata is set.

`--safe-mode` establishes a process-owned temporary settings/session root before
any WindowSlot/controller/theme/layout construction, isolates invalid preferences
and inherited renderer choices, disables ordinary original-root resume, and keeps
explicit image opens and theme/report/screenshot/timed-exit flags usable. Allow
--no-resume. Reject safe-mode with reset or maintenance. All original settings,
bak/damaged/sessions/recovery bytes stay unchanged; a later normal launch sees
them. Temporary ownership outlives final writes/value-owned jobs; never sweep
unknown TEMP folders. Feedback uses existing title/status through setSafeMode;
no new QML. Read applicable native UI skills before changing visible behavior.

Preserve QQuickRhiItem. Request D3D11/software device on Windows, software OpenGL
on supported Linux before GPU initialization, removing inherited Qt Quick software
scenegraph override process-locally. Record actual backend/frame/RGB on generated
media; requested flags are not proof. Unsupported fallback is reported honestly.
Physical GPU/driver/DPI/power-loss remains unverified unless actually measured.

Headless verbs: --settings-backup <new-directory>, --settings-diff <package>,
--settings-restore <package> with required --settings-preview <sf1-fingerprint>.
Scope flag --settings-scope global/viewing/browsing/appearance/bindings/performance/
position/layout defaults global; position/layout need --settings-slot[1,1000000]
and use offline ownership. --settings-source primary/last-good defaults primary.
No maintenance invocation constructs viewer, WindowSlot, ThemeManager, PaneLayout,
QML engine or QGuiApplication. Reject multiple verbs, image/reset/safe/theme/UI/
render/report options with maintenance, orphan scope/slot/source/preview flags,
preview on backup/diff, and malformed/ambiguous operands before mutation. Help and
version write nothing. Emit bounded schema1 JSON results with stable code names,
revisions/fingerprint/classifications/rows and explicit truncation/error. Exit0
for Ok/NoChange/Committed,2 for argument errors,3 Conflict,4 Cancelled,5 Unsupported,
6 Corrupt/LimitExceeded,7 Busy/Unreadable/WriteFailed. JSON diagnostics stay local.

## Required tests and integration

Lead build amendment, October20,2026: include the actual private policy, diff,
restore, value-owned session worker and headless maintenance adapter sources in
RecoverySources. Add dedicated recovery_controller_tests/recovery_controller
for actual controller/decoder/lazy-resume coverage (120s timeout), retaining all
existing tests. Thread linkage is explicit. Controller tests compile the actual
feature worker with private hooks for deterministic blocked-transaction/reassignment
coverage; viewer and ordinary consumers keep the production common engine with
no private hooks. This preparation is not a gate result or feature completion.
The startup environment implementation is an actual viewer/startup-test source.
Production RecoverySources is included outside BUILD_TESTING, with idempotent
provider-consumer attachment after ordinary target creation even before the full
process-test fragment is enabled. The required process test remains mandatory
before candidate qualification; early release builds are not that gate.

Lead-prepared cmake/RecoverySources.cmake is enabled for the actual feature outside
BUILD_TESTING so release builds receive the same engine and startup code. It
shares the production engine without private hooks; recovery_tests compiles its
own actual engine with private fault hooks. Existing controller/slot test consumers
keep one definition of each provider source. cmake/RecoveryTests.cmake is enabled unconditionally by the lead in
this feature's viewer/testing configuration once actual sources/tests exist,
before any candidate gate (same preparation pattern as FS-WATCH). Required targets:
recovery_tests, recovery_startup_tests and recovery_process running
scripts/verify-recovery.py with generated config/media and actual executable.
The Linux build stage includes Xvfb/Mesa and scripts; process tests create an owned
X server with TCP disabled when needed for actual software-RHI frame/RGB evidence,
and terminate only that server afterwards. Never silently skip the rendering proof.
No EXISTS guard, opt-out, skip-on-missing-Python, placeholder pass, old test removal
or grid_ui_tests growth may qualify a candidate. Python absence fails configure.
Backend/startup/process timeouts180/120/180s; keep old target timeouts unchanged.

Cover raw absent/empty/format/backup revisions; package tamper/missing/duplicate/
future/oversize; depth64/65,4096/4097 and aggregate diff bounds; immutable plans,
namespace/scope/package/policy conflicts, unknown nested data, valid/invalid sources,
corrupt current with/without base, future primary with valid backup, destination
collision, lock/preimage/atomic-write failures, cancellation before/after seal and
commit, process interruption and original/other-slot hashes. Test claim stale
nonce, same-process reclaim, pid/start reuse, legacy/ambiguous sessions, offline
contention, sequence ordering, empty-folder checkpoint, failed decode, no-navigation
clean exit, failed final save, explicit-open precedence and late claims.
Private construction/fault hooks compile only into recovery_tests, never viewer.

Process tests exercise Unicode/--/option combinations, no UI in maintenance,
safe-root original hashes, real explicit-open/lazy-resume/restart and actual safe
RHI frames/RGB where supported. Process kills do not prove physical power loss.
Preserve all existing tests/requirements. Independent cross-lane review and full
scripts/build.ps1 -Test plus docker build -t glancelot-browser:main . on candidate,
serial merge and actual integrated checks are required before done. Hardware
limitations and failed runs remain explicit. Contracts return to the lead if
implementation needs a shared interface change.

Lead build correction, October20,2026: attach recovery to every existing target
that compiles settings_file.cpp as well as viewer_controller/window_slot. In
particular pane_layout_tests and favorites_tests need the real raw reader through
the shared engine. Preserve their original tests. Declare all three recovery
catalogs with PREFIX / and BASE assets, matching the production resource alias
/themes/catalog.json; an existing source QT_RESOURCE_ALIAS themes/catalog.json
must not be prefixed with /themes again. Actual first full Windows/Linux linkage
failures and dedicated policy failures are retained in SAFE-MODE-01 verification.
These corrections fix the required wiring, not test assertions or product policy.
