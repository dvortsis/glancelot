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
