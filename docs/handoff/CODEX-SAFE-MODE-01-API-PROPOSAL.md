# SAFE-MODE-01 API and startup refinement proposal

Planning only against main f318a72 and the current UI-SETTINGS/Filter/Cache
scaffolding. This does not freeze declarations or start another Codex
implementation. Read CODEX-SAFE-MODE-01-CONTRACT-PROPOSAL.md first; its complete
F064 and F057 engine scope, bounds and required gates remain unchanged.

## Choose recovery operations, keep the checked editor private

Prefer a new settings_recovery.hpp facade containing capture-backup, preview-diff
and checked-restore operations, rather than exposing an arbitrary public JSON
editor with a caller-controlled "skip preimage" option. Refactor the existing
settings writer into one private locked pipeline; adapt existing void editors to
its normal commit path. The recovery path must always compare raw revisions and
seal an exclusive preimage before primary replacement. It cannot opt out of those
protections. Do not recursively call updateSettings from a locked editor.

Proposed value types to freeze later:

- RecoveryCode: NoChange, Committed, Conflict, Cancelled, Unsupported, Corrupt,
  Busy, Unreadable and WriteFailed. A committed replacement is never reported as
  cancelled. Preserve existing SettingsStatus numeric values and callers.
- RawSettingsMember: presence, bounded exact bytes, SHA256 and parse classification.
  RawSettingsPair holds primary plus optional backup and its versioned raw revision.
  A read error or oversized member produces no applicable snapshot or plan.
- BackupCaptureResult: code, sealed package revision and destination; never
  silently overwrite an existing destination. Fixed members only, manifest last.
- RestorePreview: bounded diff rows, known/preserved classification, truncation,
  expected current raw revision, verified package revision and explicit scope.
  No applicable plan may be constructed from a truncated/invalid preview.
- RestorePlan: engine-created immutable value owning validated replacement data
  and source revision. Do not retain pointers into mutable UI preview objects or
  re-open arbitrary manifest paths at the commit point.
- RestoreResult: code, resulting raw revision and unique preimage location when
  one was sealed, including an inert preimage left by a pre-commit failure.

Capture takes the existing settings lock long enough to read the primary/backup
pair consistently, then releases it before writing the new backup destination.
Restore validates/copies package bytes before entering the writer, compares both
current raw members under the writer lock, builds the protected preimage, checks
cancellation, then atomically replaces primary. NoChange, Conflict and Cancelled
before commit must not write primary, .bak or .damaged. A cooperating writer's
format-only edit or backup-only change invalidates an older plan. A noncooperating
writer cannot be excluded by this lock; state the bounded-read/hash limitation.

Raw revision encoding should be a domain/version tag followed by each member's
presence flag, fixed-width length and SHA256, then SHA256 the whole encoding.
Absent and present-empty must differ. Specify byte order and missing-member hash
encoding in the final contract; do not use ambiguous string concatenation.

## Current parse and section ownership

Current parseExisting accepts a missing primary as Ok/empty; a present file must
be a JSON object with schema1. Empty/invalid JSON is Corrupt. A different/missing
schema is Unsupported. readSettings may substitute a valid .bak for a corrupt
primary, so it cannot supply the raw pair needed by the preview or checked commit.
Capture raw state before that substitution. Corrupt bytes are recoverable evidence,
not a JSON object whose unknown fields can be reconstructed; preserve exact bytes
and report that limitation instead of claiming a lossless logical merge.

Known fields observed in the current writer include boundary, wheel, filmstrip,
tree, treePinned, textScale and reducedMotion; sort is a nested object; bindings
is a per-command map. appearance is a nested theme object. windows and layouts
are per-slot objects whose unknown nested fields and future versions survive.
The final allow-list must be derived from the actual setters and validated enums,
not UI-SETTINGS resetSection's deletion list. Filter is currently session state;
Cache metrics/scaffolding do not create a persisted recovery namespace. Re-check
new owner contracts before promotion instead of inferring fields from UI labels.

Restore scope must identify global known sections and, when requested, the owned
window slot. Never silently replace all windows/layouts/bindings/appearance objects.
Other slots, unknown nested fields and unsupported future layout versions remain
unchanged. Bindings unknown command IDs must survive known-binding restoration.
An apply requiring per-slot ownership must reject an unavailable lease, not fall
back to slot0 or another slot. Freeze the exact lease/maintenance-CLI contract
before implementation; backup capture itself need not claim or open a window.
The F057 checkpoint/dirty-clean engine still updates only its owned slot and
never eagerly scans all saved image/share paths.

## Early startup intent

Use one option-registration/parse definition for the early startup intent and the
final parser, including unknown/malformed options, Unicode arguments and --.
Windows must obtain the original Unicode argument spelling before Qt application
construction; do not round-trip an image path through a lossy narrow argv.

Capture the original GLANCELOT_CONFIG_DIR override and whether the platform
default was used before redirecting anything. If no override exists, retain a
location descriptor and resolve the original namespaced default after the normal
Glancelot application/organization metadata is established. Never derive the
original location by reading settingsDirectory after safe-mode redirection.
Safe-mode preferences/session coordination use a process-owned temporary root
before any ViewerController, PaneLayout, ThemeManager or WindowSlot is created.
Do not let the original sessions.json be claimed or normal resume run there.

Proposed combinations: safe-mode permits an explicit image and ordinary report,
screenshot and timed-exit verification flags; theme overrides affect only the
private session. Reject safe-mode with reset-settings or backup/diff/restore
maintenance commands, since their target config would otherwise be ambiguous.
Maintenance operations validate options and finish without constructing viewer UI.
Missing/invalid combinations perform no settings mutation. Freeze exact option
names/help and per-slot restore selection before coding.

Safe mode selects a supported RHI path and requests its software device before
window creation, removing an inherited Qt Quick software-scenegraph override that
cannot host QQuickRhiItem. Keep changes process-local. Requested backend/env flags
are not rendering evidence: require actual backend/frame/RGB reports on generated
media. Preserve the unverified physical GPU/driver, DPI and power-loss gates.
Temporary-root lifetime must outlive final settings writes and any bounded worker
that still owns it; an abandoned worker cannot write through a dead context or
recreate user configuration. Never sweep unknown TEMP directories as cleanup.

## Required proof before promotion/approval

Freeze the exact facade, raw encoding, known-field policy, lease semantics, startup
option matrix, shared declarations and unconditional test wiring under the lead
lock after the FS-WATCH implementation handoff. Then implement the entire safe
startup/diff/backup/restore and crash-recovery packet, not only the transaction.
Required fault/cancel/process-kill coverage from the original proposal remains.
Add parse cases for missing vs empty primary, backup-only edits, schema mismatch,
unknown nested binding/layout data, corrupt-without-parseable-base restore and
safe-mode plus --/Unicode/maintenance conflicts. Use generated config/media,
source hashes and actual RHI output; full Windows/Docker, cross-lane review and
integrated-commit gates still apply. Process-kill tests are not physical power-loss
qualification. No public header, product source, queue promotion or test target
is changed by this planning document.
