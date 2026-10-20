# FS/Cache private revision protocol implementation contract

Lead proposal for the frozen Cache integration amendment. Freeze its exact
worker extension under the integration lock before changing worker/controller
protocol code. No public header, external plugin ABI or thumbnail wire change.

## Use the existing process boundary for uninterruptible probes

Prefer native revision probes inside the isolated decoder over a new in-process
validation pool. A filesystem probe can block indefinitely; putting it ahead of
every decode on a one-thread pool would strand later local opens behind it.
The existing process deadline/cancellation can bound that failure while a newer
selection starts its own fresh worker. No new per-asset watch or unbounded job
queue is introduced. Foreground requests cancel prefetch before starting;
prefetch starts only while the foreground worker is idle.

Add a private decoder mode with arguments `--source-revision <absolute-path>`.
It performs metadata probes only, emits one JSON line
`{"phase":"revision","sourceToken":"r1:<64 lowercase hex digits>"}`
and exits0. Unknown/ambiguous revision emits an empty string token; malformed
requests/error paths remain bounded error responses. No image allocation,
shared-memory admission or stdin wait is part of this mode. Keep the existing
64KiB controller control-message cap and15s process deadline, including probes.

Ordinary image decoding captures a native FS source revision before opening
the source and checks it again after decode/conversion/copy, immediately before
the decoded response. Known changed/lost revision fails without publishing the
pixels. The decoded response adds sourceToken for a proven equal before/after
revision. Unknown evidence may support a fresh uncached decode, never admission
or a hit. Preserve existing dimension/allocation checks and the lock-free
worker-copy/parent-ack protocol from a6bda32. Never restore a shared semaphore
held while copying. Thumbnail server request/reply framing remains unchanged.

## Private token and cache representation

Reuse fs_watch::Revision/probe in a private helper, without calling it from
GUI/render code. An eligible source is a known nondirectory, nonlink entry with
unambiguous identity (the existing identity() qualification). Token is `r1:` plus
SHA256 hex of a versioned/domain-separated fixed-width binary revision encoding.
Include volume/object/birth/size/modified/change, link count and classification;
encode integers explicitly little-endian. Do not pass64-bit IDs through JSON
floating-point numbers. No source path or media bytes enter this token.

Controller accepts only empty or the exact version/prefix/length/lowercase-hex
shape. A malformed/missing revision response cannot admit or reuse pixels.
Cache entries store pixels plus their verified token and are indexed by a pure
text normalized path key; no QFileInfo size/time lookup occurs on the GUI thread.
The same path's newer revision replaces its old entry under the existing LRU
capacity/cost policy. Keep metrics and CacheHit spans honest.

## Foreground and prefetch state

For a navigation-open with an existing candidate entry, launch revision-only
mode. A nonempty matching token permits a real cache hit without image decoding.
A mismatch/unknown result removes/rejects the old candidate and starts a fresh
ordinary decode. An explicit open bypasses reuse and always decodes. A cold
navigation miss starts the ordinary decoder directly, avoiding a redundant
probe-only process. Bind every response to process identity and selection
generation before touching state. A cached frame never becomes current merely
because its path, size or mtime match.

Persist the navigation reuse intent when open consumes the caller's one-shot
navigation marker, rather than reading that marker after the registration
baseline delay. A nested explicit open cannot inherit it. A late registration
baseline's required fresh re-decode bypasses reuse. Keep newer explicit opens,
local-delete reconciliation and filter/traversal ownership unchanged.

Both foreground and speculative decoding admit pixels only with a valid verified
sourceToken. Prefetch's decoded response is bound to lookahead generation and
worker identity, and its process has its own15s deadline. Changed/unknown source
must not create a stale cached frame. Preserve genuine unchanged-source prefetch
hits, existing capacity/admission accounting and cancellation semantics.

Bound imported lookahead discovery to one running and one newest pending task;
do not enqueue a new QtConcurrent watcher/job on every image change. Jobs own
their navigation copy/stop token by value and cannot retain the controller after
shutdown. Preserve the existing bounded navigation pool shutdown behavior.

## Required proof

The combined generated regression first requires an unchanged-source hit, then
replaces a noncurrent image outside all three watches at equal4096 byte size and
restoredmtime1700000000. Require fresh blue pixels, cache miss, unchanged hashes
and the same traversal scope. Preserve both observed red outputs: baseline merge
loses genuine hits; the intent fix restores them and exposes stale red pixels.

Add controlled source-change/cancellation/newer-open cases at revision-only and
decode/prefetch publication boundaries, malformed/unknown token rejection,
revision worker deadline/termination, and bounded discovery. Use private test-only
seams or generated process fixtures; no production setting/hook. Run original
Cache tests and all FS tests, source hashes and original cancellation evidence.
Then actual independent Claude combined review, full Windows/Docker and serial
integrated-commit gates. These are not physical/network/coarse-timestamp proofs.
