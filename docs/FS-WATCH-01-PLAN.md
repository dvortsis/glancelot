# FS-WATCH-01 frozen contract

Frozen by the user-appointed lead on October 19, 2026. Implements F063 under
META-04, NAV-08 and SYS-02; preserves REQUIREMENTS.md and existing traversal,
selection, cancellation and original-media rules.

Lane Codex. Branch codex/fs-watch-01. Worktree .worktrees/codex-fs-watch-01.
Build build-codex-fs-watch-01. Independent reviewer Claude Code.
Start only after PLATFORM-01 reaches review with required candidate checks.
PERF/PLATFORM integration may await independent approval; do not overlap active
Codex implementation packets.

## Interfaces and ownership

No shared public C++ header change is required. Use the existing
ViewerController::trigger, filesInvalidated and state/error signals, and existing
model refresh/invalidation entry points. Add command refresh (label Refresh,
default F5, workspace scope) in a per-task FS-WATCH-01 command-table block. The
generic shortcut route handles it in preview. The October 19 routing amendment
assigns Codex the sole narrow Main.qml change of adding refresh to
gridAllowsCommand, so the same workspace command reaches grid/combined focus.
No second dispatcher or precedence change is permitted. Any need to change a shared header
returns to the lead before implementation.

Private watcher/revision helpers may live under src/app or src/core. Models and
controller may use private source identity to reconcile renames; no stable plugin
ABI is introduced. The only product QML edit permitted is the refresh allow-list
entry above; all other Main.qml and product QML behavior remains with its UI lane
owner. Do not change bindings/input precedence owned by INPUT-02, Gemini's
platform adapter, or cache/scheduler policy.
Read the required native UI skills if implementation or review reaches UI behavior;
apply them to Qt/QML and preserve existing branding/tokens without web frameworks.

The prepared test-target contract is cmake/FSWatchTests.cmake. The lead wires it
into root CMake on the feature branch under the serial lock once the real new
test file exists, before required candidate builds. Its target must be included
unconditionally in the review candidate's viewer/testing configuration. No
existing test is skipped, disabled or replaced to obtain a passing packet.

## Required behavior

1. Watch current directory, its parent and current image with at most three
   filesystem handles per window. Never watch individual delegates or recurse
   into the directory tree. Scope changes revoke older notifications/results.
2. Coalesce changes into at most one running reconciliation and one pending
   newest request. Debounce short bursts, but impose a maximum event delay so a
   continuous stream cannot postpone recovery forever. Bound queued callbacks,
   revision/identity storage, worker lifetime and shutdown waiting.
3. Watch registration, filesystem probes/listings and identity/revision work run
   off the GUI/render threads. Values returned to the GUI carry scope generation
   and source revision; an old directory or decode cannot replace a newer choice.
   Filesystem calls may be uninterruptible: cancellation and close must still
   release the UI within a bounded interval, with safe by-value/guarded lifetime.
4. Recover from watch loss, registration failure and missed/overflowed events by
   bounded fallback reconciliation, retry/backoff and explicit refresh. Qt's
   portable watcher does not report every OS overflow; do not claim otherwise.
   Rearm current-file monitoring after atomic replacement/rename saves. A failed
   watcher is degraded monitoring, not successful coverage.
5. Reconcile create, replacement, rename and delete without writing originals.
   Changed revisions invalidate derived pixels/thumbnail epochs. A stale result
   cannot restore old pixels after replacement. Unrelated membership changes
   refresh the listing without unnecessarily reopening an unchanged current image.
6. Preserve current asset, selected paths, anchor/cursor, sort, traversal root and
   stable delegates where possible. Follow a renamed identity only with unambiguous
   evidence; equal size/time or similar names are insufficient. Hard-link or other
   ambiguous identities use an explicit safe fallback. Preserve unchanged path
   selections, and test selected noncurrent assets as well as the current image.
7. Do not infer deletion from an inaccessible or truncated snapshot. Retain
   recoverable state and expose the existing error/status route. Actual deletion
   uses the established deleted-current anchor/empty-folder behavior, and a local
   trash operation plus its watch event must not advance twice. Follow the same
   renamed root object only when proven; never silently widen an intact root.
8. Empty folders, current-folder deletion/rename, unavailable parents and later
   recovery remain usable. Explicit F5 takes a fresh snapshot even after lost
   notifications. Preserve last-good settings and explicit-open precedence.

Use existing listing limits and cancellation machinery. Any new identity matching
must have a documented budget and refuse ambiguous/incomplete inference. This
packet does not replace PERF-ENUM, introduce a catalog, or claim that an existing
listing limit is a complete large-directory publication solution.

## Evidence and acceptance

New tests go in tests/fs_watch_tests.cpp (and a separate fs_watch_ui_tests.cpp only
if a real QML harness is needed), never tests/grid_ui_tests.cpp. Generated media
only. Include actual Windows filesystem events and Linux test-runtime events, plus
deterministic injected loss/storm/delay cases so native delivery timing cannot
conceal a race or overflow defect.

Cover external create/replacement/rename/delete, repeated atomic replacement,
selected noncurrent entries, unrelated changes, empty folders, folder/parent loss,
permission/unavailable paths, rapid A-to-B navigation with delayed A results,
bounded event storms/queues, forced watch loss/overflow recovery, explicit refresh,
local-delete duplicate notification, cancellation/close and unchanged source hashes.
Test path/identity ambiguity and truncated snapshots conservatively. Record actual
handle/queue bounds and command results; do not rely only on manual screenshots.

Before review: the lead enables the frozen fs_watch target in root CMake under
the lock, then run scripts/build.ps1 -Test on Windows and the full Docker build.
The full test list must include fs_watch and the required real-QML fs_watch_ui
target. The latter exercises actual F5 in preview/grid/combined modes and
text-input/settings-modal precedence using the existing dispatcher. Skipped assertions do not establish
acceptance. Record commands, terminal output, failure/cancel cases, source hashes
and limitations in docs/verification/FS-WATCH-01.md.

Independent Claude review, serial integration and full checks on the integrated
commit are required before done. Physical/network-share and other filesystem or
driver coverage remains unverified unless actually measured. Preserve the separate
CANVAS-UI, DECODE-01, color and performance hardware gates.

## Reviewed decoder prerequisite amendment, October 19

FS-WATCH reload cancellation must not freeze the GUI while discarding a transfer.
The lead permits importing only Claude-authored standalone commit a6bda32
(src/worker/main.cpp semaphore removal) into this packet. Independent Codex
review db23447 APPROVED that isolated Windows protocol change: complete64MiB
payload/ack and five witnessed mid-copy kills detached in13-17ms; the old worker
reproduced a60s detach hang. Preserve the unchanged report
docs/verification/DECODER-CANCEL-INDEPENDENT-CODEX-REVIEW.md. This imports an
independently reviewed prerequisite; Codex does not take ownership of INPUT-02 UI.
No shared header or protocol message changes. Production reads pixels only after
decoded and acknowledges only after copying. The five INPUT UI blockers remain.

This scoped Windows review does not prove Linux cancellation or whole-controller
teardown. Run the packet's complete native and Docker gates on the resulting
source, record failures honestly, and include cancellation in independent Claude
review. An inherited or intermittent failure cannot be counted as a full pass.

## Shared Windows qualification prerequisite

The lead assigns the frozen test-only Windows launcher fixture from
docs/PLATFORM-01-PLAN.md to this active worktree. Its four tests/ files and the
lead-owned cmake/PlatformTestFixtures.cmake inclusion are permitted prerequisites
for required native gates. Production platform adapter/public API remain unchanged.
Independent Claude64f0af0 assessed the proposal; implemented code still requires
review and full checks. Do not alter directory_model's250ms oracle or add arbitrary
drains. This does not approve the pending FS-WATCH implementation.

## Linked-scope clarification after independent FS review

Lead permits a private worker-only directoryProbe helper in fs_revision.hpp.
For a linked/junction scope directory, resolve the target at each membership scan boundary
and probe that directory for membership revision. Keep requested alias spellings
in rows, callbacks and watch registrations; never rewrite the user's address.
Capture the link entry revision too and reject completeness if it or the target
revision changes across enumeration. Ordinary entries still use no-follow probes:
links/hard-link ambiguity cannot establish an asset or alias-directory rename.
Apply the same scope probing in model listings, without any filesystem work on
the GUI thread, recursion or extra watch handles. Test generated POSIX directory
symlinks and Windows junctions, target-file rename/selection and unchanged bytes.
This adds no public header, user setting, original-media write or cloud access.

## R3 corrective policy and deterministic test seams

Lead decision after actual Claude review ecb9d54: incomplete observations must
still invalidate derived listings after the first observation (or on explicit
refresh), using existing debounce/backoff and bounded work. They never prove
rename/deletion and cannot replace complete identity baselines. Folders beyond
watch identity limits retain automatic relisting within the model's existing
limits, while status continues to report degraded inference. No new catalog or
large-directory completeness claim is introduced.

Permit private scan-boundary callbacks and controller observation/defer hooks
only under GLANCELOT_FS_WATCH_TESTING, defined exclusively for fs_watch_tests.
The test executable compiles the actual controller source with these seams;
viewer, decoder and real-QML feature binaries do not contain them. No public
header, runtime option, user configuration, extra watch handle or original-media
write is added. Hooks must use by-value/copied lifetime, restore thread-local
construction overrides and never leak across tests. Required deterministic cases:
retarget A-B and A-B-A before final scan validation; first partial then complete
observations coalesced while controller reconciliation is deferred; delayed first
baseline with exactly one fresh decode and newer-open cancellation; permanent
incompleteness still producing automatic invalidations.

Rearm a registered alias when its bound target/entry changes rather than claiming
coverage from an unchanged string. Explicit navigation through another alias
spelling of an existing directory resets selection; identity-following is reserved
for a proven rename where the former spelling no longer denotes that identity.
Physical/coarse-timestamp/network/reparse variants remain unverified until measured.

## Lead-frozen Cache integration amendment

Frozen under lead/codex-fs-watch-sync after mainb90163d releases the Cache lock.
FS source825f7f2 is independently acceptable for integration (Claude f0bf272),
but Cache main8b28060 introduces a navigation hit path that must participate in
FS revision validation. Independent Codex review16ef41f reproduces wrong pixels
after a noncurrent same-size/restored-mtime replacement. Preserve that review and
the original Cache evidence; the older approval does not cover this new case.

Assign the narrow private integration correction to the lead/Codex FS packet in
its own worktree. This aligns FS changed-source guarantees with the imported
Cache engine; it does not transfer ownership of Cache policy or start another
implementation packet. Do not edit the Gemini worktree. Preserve real decoded
hits, bidirectional traversal, cross-folder lookahead, metrics, capacity/admission
policy and public Cache declarations. Disabling all hits is not completion.

- Merge current main into the FS branch under the serial lock after its running
  immutable Windows gate finishes. Resolve private controller integration in the
  assigned FS worktree; retain newer main public declarations and feature tests.
  Do not restore the old GUI filesystem watcher/debounce path.
- Use private FS source revisions for both foreground cache reuse and prefetch
  admission/publication. Path/size/mtime alone cannot validate pixels. Check native
  identity/revision independently of notification delivery, off the GUI thread.
  Unknown/ambiguous source evidence is a miss. Capture before decode and validate
  again before publishing/admitting its result; scope/generation and source
  revision are separate guards. Do not claim exclusion of an external writer
  after the final check; later events/polling still reconcile subsequent changes.
- Bound validation to one running job and one newest pending request per window,
  with by-value lifetime and cancellation/generation checks. An uninterruptible
  probe cannot block UI shutdown. Preserve at most three FS watches; do not add
  per-cached-asset watches. Do not introduce a competing settings/catalog writer.
- Keep foreground and prefetch worker memory/deadline bounds. The imported
  prefetch path must honor its existing Cache packet's15s worker limit. Invalidate
  old work before admitting a changed revision and retain cacheChanged metrics
  notifications when stored pixels are purged. No scheduler/capacity redesign.

Add the generated noncurrent replacement regression to the separate FS feature
tests, not grid_ui_tests.cpp, while retaining original Cache tests. Require fresh
blue pixels and a miss after the equal4096-byte/restored-mtime red-to-blue change;
require genuine hits/lookahead for unchanged sources. Include cancellation/newer
open, source change during decode/prefetch and watch-loss fallback without new
original-media writes. Existing private test-only seams may be extended under
GLANCELOT_FS_WATCH_TESTING only; no runtime hook or shared header change is frozen.
No additional product QML change is permitted beyond the existing refresh token.

Return the combined candidate to independent Claude review. Run full Windows and
Docker gates on combined source, then complete the required serial no-ff merge
and integrated-commit checks before either packet is done. Prior source-only
passes are preserved as scoped evidence, not relabeled as combined approval.

## Private worker revision extension freeze

Under lead/codex-fs-revision-protocol, freeze the exact private implementation
contract docs/handoff/CODEX-FS-WATCH-01-REVISION-PROTOCOL.md. It uses the existing
isolated decoder process for cancellable metadata validation, rather than letting
an uninterruptible probe strand a new in-process validation queue. Add the
revision-only mode and additive verified sourceToken on normal decoded replies;
retain thumbnail framing, allocation checks and the lock-free transfer/ack rule.
This is an explicitly authorized private worker/controller extension for the
FS/Cache correction, with no public-header or stable external-ABI change.

Require exact token validation, captured navigation intent, fresh explicit opens,
bounded lookahead discovery, existing15s deadlines, source-change/cancellation
regressions and original Cache features. Final combined independent review and
full platform/integrated checks remain mandatory. No prior green result is
approval of this still-unimplemented protocol extension.

The concurrent IO header freeze adds ViewerController properties before its
owner's scheduler wiring is imported. Lead-owned linkable defaults preserve
the shared declarations during this integration: auto/Unknown/zero and a no-op
setter, with no filesystem work or claim of IO functionality. The IO owner
replaces that explicit default block in its packet. Preserve its uncommitted
work; FS does not implement the scheduler or change its policy/API.

## Lead-frozen IO admission ownership correction

Under lead/codex-fs-io-sync, extend the narrow integration scope to reservation
identity/lifetime correctness required by source-validation workers. Imported
IO1f97893 uses independent selection/lookahead/thumbnail counters in one shared
ledger, and cancel can release the new selection's ID instead of the old worker's
reservation. Focused regression4c11c50 also proves duplicate-ID overwrite
(3072 bytes charged to one entry) and negative-estimate acceptance,2 pass/2 fail.
The original IO review/gates did not exercise these cases; preserve that evidence.

Freeze glancelot::allocateIoJobId() in io_scheduler.hpp: nonzero process-unique
opaque IDs, never reused even by reset(); zero means exhausted. This is a free
function, not a new virtual policy method. Reject duplicate/zero IDs, negative
estimates and arithmetic overflow without changing existing reservations. Preserve
existing profile, detector, concurrency and capacity policy; this is not a new
IO scheduler implementation packet.

Every foreground/revision-only/prefetch/thumbnail worker owns a separate private
admission lease, distinct from source generations and thumbnail wire request IDs.
Only successful admission can create a lease; cancellation cannot release another
job. Keep reservations until actual job completion or retired-process exit,
including cancelled/stale thumbnail work. A metadata-only foreground probe owns
zero pixel bytes; fresh image admission replaces that lease with its pixel cost.

Freeze IoScheduler::admissionAvailable(), emitted outside its mutex after release
or capacity increase, so deferred consumers can retry. Thumbnail retries must be
coalesced/bounded and must not recurse into pump during worker retirement. Add
real ownership/cancellation/wakeup regressions in FS feature tests, preserve
original IO/Cache tests, and return the combined source to independent Claude
review and full platform/integrated gates. No new product QML is permitted.

### Controlled IPC verification amendment

Freeze the test-only fs_watch_decoder_fixture target (QtCore/Gui, uninstalled)
and compile the real ThumbnailService into fs_watch_tests. The fixture accepts
at most 8 KiB of generated JSON controls outside watched media, produces only
generated 32x24 pixels, and implements revision/probe/shared-memory/decoded/ack
framing. It does not read or write media. Private ControllerTestHooks may copy
decoder program/argument overrides, positive test deadline overrides (including
baseline wait for IPC cases; production registration fallback remains 100 ms), guarded
worker-start observers, copied cancellable discovery, bounded state observers
and a weak-controller request closure. All hooks remain exclusive to the
GLANCELOT_FS_WATCH_TESTING backend target. Production deadlines remain 15 s.
Require malformed/empty tokens, superseded and timed-out workers, bounded
discovery (one running/one latest pending), real thumbnail capacity wakeup and
cross-service admission identities before combined independent review. Preserve
existing native decoder tests and full Windows/Linux/integrated gates.

### Current-main controller adapters

During FS integration, preserve imported metadata/animation/mutation/RAW features.
Adapt metadata dispatch only (not its parser/model policy) to one active/one latest
pending value-owned parse with receiver-owned completion and selection generation
guards; cancellation/close must not leave callbacks referencing Private. Route
rename reconciliation through existing asynchronous controller navigation/monitor
paths instead of synchronous Navigator scans. Apply the frozen source revision
and lock-free shared-memory completion protocol to imported RAW decode as well.
These are private integration adapters, not new Gemini implementation packets.

### Required dense-folder test isolation

The full current-main Windows checkpoint passed 30/31 targets but the combined
FS backend exceeded its 180 s runtime. An unchanged single dense-folder case on
c48b77c passed in 79.396 s (setup 54.626 s; maximum GUI publication 124 ms).
Freeze a separate required fs_watch_budget_tests executable and fs_watch_budget
CTest entry containing that entire case unchanged. Both backend entries retain
180 s timeouts; keep every assertion, 100k files, 250 ms GUI and 30 s recovery
limits. This splits fixture runtime, not product behavior or acceptance. Require
both plus fs_watch_ui in Windows, Docker and integrated gates. File and stdout
QtTest sinks must preserve actual case counts/failures on Windows.
