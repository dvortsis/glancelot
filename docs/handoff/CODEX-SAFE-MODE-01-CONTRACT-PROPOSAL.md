# SAFE-MODE-01 contract proposal: F064 and F057 engine

Planning only, October 19, 2026. This is not the frozen packet or permission to
start a second Codex implementation. Queue state remains ready. The lead must
commit docs/SAFE-MODE-01-PLAN.md, shared declarations and required test wiring
before coding, after the current FS-WATCH implementation reaches its handoff.

Proposed lane Codex; branch codex/safe-mode-01; worktree
.worktrees/codex-safe-mode-01; build build-codex-safe-mode-01. Independent reviewer
Claude. Tracked action act_mvfhyfit_af2798dd4e13. Requirements SYS-03 and UX-12;
retain the full F064 safe-mode/config-diff/backup/restore and F057 crash-recovery
engine scope. Claude owns the follow-up recovery UI, not this engine packet.

## Existing authoritative behavior

settings_file.hpp freezes the single schema-1 writer: cross-process lock,
re-read/merge, QSaveFile replacement, last-good .bak, no overwrite of unsupported
schemas, unknown-key retention and nonblocking interactive contention. Its reader
accepts at most1MiB. resetSettings sets the primary aside; it is not isolated safe
mode. Existing export/import and section reset belong to UI-SETTINGS/F050; extend
shared primitives through the lead instead of adding a competing writer.

WindowSlot coordinates sessions.json with pid plus process-start identity, a
counter capped at1,000,000, no live-session reuse and no claim from damaged or
future bookkeeping. ViewerController already persists successful folder/image
positions, retains unopened slots and gives explicit opens precedence. PaneLayout
persists versioned per-slot layouts separately in the same settings object.
app/main.cpp currently constructs QGuiApplication before parsing options. These
contracts remain prerequisites, not features to reimplement or silently weaken.

## Required safe-mode behavior

1. Add an explicit safe-mode launch route with CLI help and existing status
   feedback. Capture the original config location before any redirection. Use a
   process-owned temporary settings view for defaults; normal interactions in this
   session must not modify primary, backup, damaged evidence, sessions or unopened
   slots in the original directory. A normal later launch sees the originals.
2. Isolate invalid layout and renderer overrides before the relevant initialization.
   Keep explicit image opens usable; do not automatically scan or restore every
   saved window/share. Defaults in the temporary view must not overwrite original
   bindings, themes, unknown keys or last-good positions.
3. Preserve the RHI canvas. Windows can request D3D11 and a software adapter;
   Linux may request OpenGL with the available software implementation. Record
   actual backend/render evidence; a requested fallback is not proof it rendered.
   Qt Quick's software scenegraph is not an interchangeable QQuickRhiItem backend.
   No machine-wide driver, environment, service or power-setting change.
4. Parse early startup intent consistently with the final parser, including --,
   malformed options and Unicode image paths. Decide and freeze maintenance-command
   combinations before implementation; no ambiguous silent restore on launch.

Qt6.8 documents QQuickGraphicsConfiguration's software-device preference, including
QSG_RHI_PREFER_SOFTWARE_RENDERER. The request depends on the API/device and can be
ignored when unsupported. Qt6.8.3 source maps it into QRhi flags. Sources:
[Qt graphics configuration](https://doc.qt.io/qt-6.8/qquickgraphicsconfiguration.html#setPreferSoftwareDevice),
[Qt6.8.3 RHI support](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.3/src/quick/scenegraph/qsgrhisupport.cpp).
Test the exact project Qt6.8.3 behavior; do not infer fallback from environment
variables or from a successful offscreen software-scenegraph unit test.

## Backup, diff and restore engine

- One bounded engine, with filesystem work on a worker or a maintenance path before
  the interactive UI starts. A future UI adapter must submit asynchronous jobs;
  cancellation/generation guards prevent an obsolete preview becoming applicable.
- Propose a versioned backup directory containing only fixed settings.json and
  optional settings.json.bak plus a manifest sealed last. Preserve captured bytes
  exactly, classify validity separately, and hash each member. No recursive config
  enumeration, arbitrary manifest paths, media, executable code or cloud upload.
  Capture the primary/last-good pair under the existing lock, then release it
  before writing the destination. Never overwrite an existing backup silently.
- Bound each settings member to the existing1MiB reader limit, manifest8KiB,
  members2 and aggregate package3MiB. Oversized/unreadable sources fail honestly
  while originals remain untouched. An incomplete/unsealed backup is not valid.
- Preview validates version, member names, sizes, hashes and schemas. Invalid,
  future or tampered packages cannot produce an applicable restore plan. Diff
  output identifies JSON paths, addition/removal/change and known/unknown status;
  bound depth64 and rows4096, report truncation, and never apply a partial diff.
- Restore compares both the sealed backup revision and the expected current raw
  settings revision under the same settings lock. A stale plan returns Conflict;
  never overwrite another window's newer preferences. Preserve an exclusive,
  uniquely named exact preimage before replacement; a preimage failure aborts.
  Keep last-good/damaged originals and other slots recoverable at every stage.
- Freeze a known-section policy against the final UI-SETTINGS schema. Existing
  unknown keys and nested unknown fields remain unchanged; the source backup stays
  intact. List what will change or be preserved in the preview. Do not replace
  entire layouts/bindings/appearance objects in a way that discards future fields.
- Reuse one atomic transaction implementation. The current void edit callback
  cannot veto after the lock/re-read. The lead must freeze a checked transaction
  extension with typed abort/conflict status before restore implementation. Do not
  acquire the same lock recursively or bypass updateSettings with another writer.
- Cancellation before commit leaves the primary unchanged. Once atomic commit has
  happened, report success/too-late honestly; cancellation cannot claim rollback.
  Expose bounded statuses/results suitable for Claude's later UI without new QML.

## Crash-recovery restore engine

Use existing successful per-slot positions/layouts and ownership, not a second
catalog or a scan of saved source paths. Freeze an additive, versioned recovery
namespace with dirty/clean state, process-start identity and checkpoint revision.
Only the owned slot may be updated; slot0 never writes a shared fallback record.
Preserve unknown recovery records and future versions without downgrade.

Write successful checkpoints through the same atomic transaction as their saved
position. Failed decode/navigation never replaces a last-good checkpoint. Mark
clean only after final persistence succeeds while slot ownership is still held;
a busy/unwritable final save stays degraded/dirty rather than claiming clean.
Use bounded queues/retries and the existing explicit-open generation precedence,
including a slot claimed late. Lazy restore must leave unopened slots untouched,
handle missing/empty/unavailable sources and never overwrite a newer explicit
selection with an older restore result. Renderer resources, pending file mutations
and completed-operation journals belong to their own packets.

Interrupted migration/restore tests must prove that the primary is either old or
new, a valid backup/preimage survives, and unknown fields/other slots remain.
Simulated process kills are not evidence of an actual physical power-loss trial.

## Test and implementation ownership to freeze next

Prepare required tests/recovery_tests.cpp and a separate process harness
scripts/verify-recovery.py using generated media/preferences only. A real-QML
feature test, if needed, goes in tests/recovery_ui_tests.cpp, not grid_ui_tests.
The lead wires all targets unconditionally in the packet before candidate builds.
Product QML remains owned by its UI packet; any necessary exception returns to the
lead, with native UI skills and unchanged tokens/branding.

Cover bounded malformed/deep/future input, schema migration, backup tampering,
missing members, stale revisions, lock contention, concurrent windows, destination
collisions, preimage/atomic-write failure, cancellation around commit and process
kill at migration stages. Run real explicit-open/lazy-resume/restart/safe-mode
process cases with source/config hashes. Verify fallback with actual frame/RGB
output on supported software backends; physical GPU/driver and power loss remain
unverified unless measured.

Before promotion: freeze exact shared API, known-section schema and startup flag
combinations against current main; do not reduce the packet to --reset-settings or
an export wrapper. Required Windows build.ps1 -Test and full Docker, independent
cross-lane review, serial integration and integrated-commit checks remain required.
