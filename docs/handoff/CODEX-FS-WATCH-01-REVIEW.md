# CODEX FS-WATCH-01 independent review handoff

Implementing lane: Codex. Assigned branch codex/fs-watch-01; worktree
.worktrees/codex-fs-watch-01; build build-codex-fs-watch-01. Reviewer: Claude Code,
with its own isolated worktree/build. Preserve docs/REQUIREMENTS.md, the frozen
FS-WATCH-01 plan, lane ownership and original-media immutability.

## Candidate and scope

Product source is b9fa03b4b355c5412527f6c2de07aefa9607bbda; later commits only
record evidence. Review the exact handoff commit assigned by the lead, not a moving
branch. Lead contracts permit only refresh in Main.qml::gridAllowsCommand and the
required fs_watch/fs_watch_ui targets. Root CMake and Docker cmake source-copy
wiring were performed by the lead. No shared public C++ header was changed.

The worker semaphore prerequisite is Claude-authored a6bda32, imported unchanged
as615c0b5 after independent scoped Codex approval db23447. Its exact source hash
matches the independently compiled worker. That scoped Windows review is not
INPUT-02 UI approval; the five INPUT UI blockers remain separate.

## Behavior to inspect

- One monitor worker/event loop per controller; current directory, its parent and
  current image only, at most3 registered paths. Registration/probes/listings and
  identity matching are worker work. Debounce120ms, maximum500ms; timed fallback
  and failed/incomplete retry backoff2-30s; explicit F5 bypasses backoff.
- Scope generations/revisions, one latest requested scope, one coalesced queued
  publication and one controller pending observation. Manual baseline acknowledgement
  preserves the earliest unprocessed identity while navigation is busy. Inspect
  mutex ownership, receiver teardown and the bounded abandoned-worker lifetime.
- Directory snapshots cap20,000 entries and16MiB conservative identity bytes each;
  directory revision is checked around enumeration. Model identity maps have the
  same byte cap within the existing100,000 listing limit. Incomplete, unreadable,
  hard-linked, reparse or ambiguous evidence refuses rename/deletion inference.
- Current and selected noncurrent identities follow proven renames, even if a new
  object reuses the old file/directory pathname. Windows case-only spelling is
  retained. Unrelated membership changes do not reopen an unchanged current image.
  Replacements invalidate derived pixels/epochs, including same-size restored-mtime
  changes. Preserve sort, anchors/cursor, widened roots and last-good navigation.
- Actual deletion uses established recovery; local Trash plus watch notification
  cannot advance twice. Failed/truncated snapshots retain recoverable state and
  error ownership. Explicit open has priority over delayed scans/decodes.
- Decode normally waits for watch baseline with100ms fallback; stale generations
  cannot restore pixels. The reviewed worker writes before decoded and parent copies
  before acknowledgement. Inspect whole-controller cancellation as well as the
  separate independently witnessed partial-transfer proof.
- F5 uses the existing dispatcher in preview/grid/combined. Text and modal precedence
  remain intact. No watcher per delegate, original-media writes or policy changes
  to Gemini's cache/scheduler/platform adapter.

## Actual checks and unresolved gates

Current product source, after the worker prerequisite and whole-controller large
cancel regression, passed required Docker20/20,144.99s. fs_watch31.43s and
fs_watch_ui1.14s. Image
733559bd1d8fc078d006b92c112680e6c2cc340da3c1390f238e1beff257134e.
Log .cache/fs-watch-worker-docker.log. Linux native events and controller
cancellation tests ran in the container; no physical Linux display claim.

Earlier source b4c9b04 passed Docker20/20,139.50s and Windows feature targets2/2,
35.20s. Its full Windows gate FAILED18/20,346.02s (directory responsiveness and
platform-launch timeout). Exact diagnostics are preserved; no skips or relaxed
250ms limits. Current new-source build passed; an overlapping Windows full suite
was intentionally cancelled during folder_grid to preserve clipboard serialization.
A fresh serialized Windows gate is now running; do not infer its result.

The independent decoder host checked every64MiB RGBA byte and normal ack/exit,
then five post-stop witnessed partial-copy cancellations detached in13-17ms. The
old baseline hung at detach until60.258s supervisor timeout. All generated source
hashes stayed unchanged. See DECODER-CANCEL-INDEPENDENT-CODEX-REVIEW.md; this is
scoped proof, not every shared-memory interleaving or whole-app approval.

New tests cover native events, deterministic registration/watch loss and rearm,
storm max-delay, delayed scopes, blocked close, selection/current/directory rename,
old-path reuse, replacement, duplicate local deletion, explicit refresh/root
recovery, identity ambiguity/hard links/byte limits and unchanged fixture hashes.
Real-QML F5 tests live separately in fs_watch_ui_tests.cpp. No new test function
was added to grid_ui_tests.cpp; old rename-selection expectations were updated
while retaining removed-item assertions.

## Reviewer rules and deliverable

Read project/lane/frozen contracts and apply application-interface-design plus
applicable native layout/spacing/typography/geometry/iconography/motion/responsive
and design-review principles to Qt/QML. Preserve supplied branding and tokens.
Inspect the actual scope, failure cases, cancellation and budgets; do not approve
from author counts or conditional identity tests alone. Reproduce meaningful
findings and capture actual output from independently executed checks.

Use only the assigned reviewer worktree/build. Do not edit production code or
shared/root contracts, another lane's worktree or WORK-QUEUE. Temporary probes
belong under its ignored .cache; restore them before reporting. Commit only
 docs/verification/FS-WATCH-01-CLAUDE-REVIEW.md with exact candidate, disposition,
findings, evidence and limitations. Do not merge/push or change machine settings.

Native full suites share the host clipboard. Obtain the lead's native slot before
building/testing; static inspection can run meanwhile. The root and assigned
feature .cache/native-suite-running markers identify an active lead gate. A marker
alone is not proof of a live process: verify the holder and actual test processes.
No concurrent full Windows suites; do not stop another owner's job. Docker does
not qualify Windows behavior or physical display/network/filesystem coverage.
