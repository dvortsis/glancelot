# CACHE-01 independent Codex revision review

Candidate: main8b28060 (Gemini implementation37a8949). Reviewer: Codex, separate
from the implementing Gemini lane. Scope is the Cache/FS-WATCH integration risk,
not a full reapproval of Cache. Isolated branch codex/review-cache-01, worktree
.worktrees/codex-review-cache-01, build build-codex-review-cache-01. Product source
is unchanged. Only a generated-media review regression and this evidence are added.

Disposition: CHANGES REQUESTED. The prior CACHE-01.md approval does not cover this
demonstrated same-size/restored-timestamp navigation case. Do not mark the combined
Cache/FS packet done from an explicit-open regression or ordinary size-change test.

## R1: noncurrent revision change returns stale cached pixels (high)

viewer_controller.cpp cacheKey uses absolute path, size and mtime milliseconds.
decode's navigation-open hit directly publishes cache.object(key) without an
independent source revision check. A watcher in the current folder cannot prove
that a cached source in another folder is unchanged. startPrefetch similarly
captures the tuple before decoding without revalidating the source afterward.

Fresh Windows execution reproduced the stale hit against the exact candidate:

- Generate root/a.png red, pad to4096 bytes and set mtime1700000000 UTC.
- Open it, then navigate next into root/sub/b.png (green). Require both entries
  cached; drain existing events while the old folder is no longer watched.
- Atomically replace a.png with blue, again4096 bytes and the identical mtime.
  Read back the generated source to prove it is a valid blue image.
- Use previous(), the hit-eligible navigation route, to return to a.png.
- Source pixel is blue; displayed pixel is red; isLastDecodeCacheHit is true.

This is an actual incorrect display result, not a predicted race or timeout.
The targeted run produced2 passed,1 failed,0 skipped in816ms, process exit1.
Exact output: cache-01/noncurrent-same-size-time-red.txt. The failure is the pixel
comparison after successful setup/navigation, not fixture creation or an absent
decoder. Media and preferences are generated QTemporaryDir contents; the original
GLANCELOT_CONFIG_DIR environment override is restored after controller teardown.

## Reproduction and isolation

Configure the isolated Release Qt6.8.3/MSVC2022 build with viewer/testing enabled,
then cmake --build build-codex-review-cache-01 --target cache_tests --parallel4.
Run with project Qt bin on PATH and QT_QPA_PLATFORM=offscreen:

    cache_tests.exe noncurrentSameSizeTimestampReplacementRejectsStaleHit -o <log>,txt

The added test resides only on this review branch. It is deliberately red against
8b28060 and is not an implementation patch to merge as a green feature. No other
lane source/worktree, shared configuration or integration lock was changed.
No full Cache gate or filesystem/hardware qualification is claimed by this run.

## Required correction before combined approval

Have the Cache owner preserve working hits/lookahead while validating source
revision independently of watch delivery. Probe off the GUI thread, bind decoded
results to that revision, and revalidate before admission/publication. Reject
ambiguous or changed revisions as misses; do not solve this by disabling all hits
or adding watches per cached file. Preserve FS-WATCH's three-watch bound,
generation guards and original-media rules. Port this generated regression into
the corrected candidate and require a green result, existing Cache regressions,
FS replacement/cancellation tests, independent combined review and integrated
Windows/Docker gates. Explicit-open bypass alone does not exercise this defect.
