# Remaining FS/Cache/IO controlled IPC verification

Implementation73737a2 has full Windows backend59/59 and original Cache10/10,
Thumbnail36/36,IoScheduler12/12 passes. These do not replace the remaining frozen
protocol failure/race requirements or independent combined review. This is the
next test implementation plan, not a new Codex feature packet.

## Prepared target to freeze under the lead lock

Extend cmake/FSWatchTests.cmake with fs_watch_decoder_fixture, a QtCore/Gui-only
console helper from tests/fs_watch_decoder_fixture.cpp; make fs_watch_tests depend
on it. Compile thumbnail_service.cpp/header into the FS backend feature harness
for real cross-service ownership/wakeup tests. Keep the helper test-only and
uninstalled; no new product flag or public API. Preserve existing FS/QML targets.

The helper accepts a generated bounded JSON control file in a separate temporary
directory from the watched media, followed by the normal decoder arguments. It
implements the real revision/probe/admission/decoded/ack framing with generated
32x24 RGBA pixels and configurable token/delay/omission per generated filename.
It never reads/writes originals. Bound control input and diagnostics; optional
events log stays in its generated control directory. No network, shell or service.

Extend ControllerTestHooks only under GLANCELOT_FS_WATCH_TESTING: decoder program/
argument-prefix override; positive test-only foreground/prefetch deadline override;
worker-start observer carrying QProcess and purpose/path; by-value lookahead
discovery override plus state observer and a weak-QPointer guarded request control.
Production/QML binaries retain the fixed real decoder and15s deadlines. Copy
hooks at construction and preserve callback/test-local lifetime ordering.

## Required cases

- Warm A/B, then give A a missing, nonstring, wrong-version/length or uppercase
  revision token. Require error without publishing cached A, no hit and complete
  job cleanup. A later explicit valid C must still work.
- Empty revision plus empty decoded token: fresh generated pixels, a miss, no
  admission into cache. Configuration changes must not themselves invalidate the
  watched media folder and accidentally bypass the lookup under test.
- Hold A's revision reply, observe the exact started process with QPointer, then
  explicitly open C. Require C stays current after the old reply window, the
  old process actually stops and all owned reservations return to baseline.
- Hold revision/prefetch workers beyond short test-only deadlines. Require the
  real timeout/cancel path, actual process termination, no stale cache admission,
  bounded close and successful later open; do not lower production deadlines.
- Block one discovery by a copied hook; issue200 requests through the actual
  private scheduler seam. Observe at most one running and one pending; cancel
  stale work, release the hook and verify only the latest result is considered.
- Real ThumbnailService: occupy its memory budget, request a generated PNG,
  verify it stays pending, release the reservation and require ready without a
  second request. Capacity wakeups must drive the existing bounded pump.
- Two thumbnail services start with the same public request ID1. Under SSD
  profile2, both admissions must coexist with distinct opaque IDs, both deliver
  and ledger bytes/jobs return to zero. Under rotational profile1, the second
  must resume after the first completes. This tests real callers, not only the
  duplicate-ID ledger guard.

Keep the actual native-worker source-token, changed-source-during-admission and
real noncurrent atomic replacement regressions. Fixtures qualify state/IPC races,
not image decoding, physical GPU, storage latency or network/coarse timestamps.
Afterward synchronize current main under the lock (META-HUD owner is active),
run required full Windows/Docker gates, obtain actual Claude independent review
from a fresh fixed worktree, then serial no-ff integration and integrated checks.
