# FS-WATCH / CACHE-01 integration proof obligations

Read-only compatibility audit while FS-WATCH validation runs. CACHE-01 remains
Gemini-owned; this is neither its implementation nor an independent approval.
Observed working source: agent/cache at f708068 plus uncommitted changes;
viewer_controller.cpp SHA256
A22BF4D26DC3233CAFA28663E4830A702C3DCF38B1E7B55215C97EBF92707BD2.
Recheck the final owner candidate rather than assuming this snapshot is final.

The observed cache key uses absolute path, size and mtime milliseconds; a
navigation-open can directly publish cached pixels when that key exists. Watch
notifications are hints and do not validate a noncurrent cross-folder prefetch.
A source replacement or same-size edit followed by mtime restoration can preserve
that tuple. The predicted stale-hit case needs a generated-media runtime test;
no failing Cache runtime reproduction is claimed by this static audit.

Before integrating the actual cache engine with FS-WATCH:

- Cache admission/hits must validate a source revision independently of delivery
  of fileChanged/directoryChanged, with safe miss/fallback for ambiguous identity.
  Do native probes, reads and cross-folder discovery off the GUI/render thread.
  Do not solve this by adding watches per prefetched asset: FS-WATCH permits only
  directory, parent and current image, at most three handles per window.
- Capture and revalidate a prefetch's source revision around decoding/publication.
  Cancellation/selection generations and source revisions are separate guards;
  a newer generation alone does not prove cached bytes still match the source.
- Preserve FS-WATCH's worker registration, debounce/backoff, initial-baseline race
  handling, manual refresh, replacement/rename/deletion semantics and source hashes.
  Do not restore the old GUI-thread QFileSystemWatcher/reload path during a merge.
- Derived invalidation must revoke stale foreground and prefetch results before
  admitting a hit; preserve cache metrics notifications when storage is purged.
  Existing public Cache scaffolding is not proof those owner behaviors exist.

Required generated test: warm/possibly-prefetch A, leave its watched folder,
replace A with distinct pixels of equal encoded size and restore its original
mtime, then navigate back through the hit-eligible route. Require fresh pixels,
correct cache-hit/miss reporting, no stale result overwriting a newer open and
unchanged unrelated-source hashes. Include registration-loss/missed-event and
rapid reversal cases. Explicit open alone is insufficient because the observed
owner code limits hits to navigation-open. Keep the frozen GUI responsiveness,
worker memory/deadline and full-platform gates. Physical/coarse-timestamp and
network filesystem coverage stays unverified unless measured.

## Recheck after Cache integration

Main8b28060 now commits the Cache engine. Static inspection of that exact commit
still finds cacheKey(path) formed only from absoluteFilePath, size and mtime in
milliseconds, with navigation-open directly publishing cache.object(key).
startPrefetch captures this tuple before decoding and inserts decoded pixels
under it without a second source-revision validation. This confirms the earlier
proof obligations apply to the committed engine; it is not a runtime reproduction
of stale pixels. Cache's integration lock remains held by agent/cache, so this
FS branch has neither merged main nor modified the owner's implementation.

The FS candidate deliberately had no cache-hit path before that merge. Integration
must preserve the delivered Cache behavior while adding the required revision
validation/invalidation synchronization; removing lookahead or disabling all hits
would not complete both contracts. Reconcile under the lead lock and have Claude
review the combined source and generated cross-folder replacement evidence.

## Independent generated reproduction

The risk is now reproduced, not merely predicted. Independent Codex review of
Gemini's exact main8b28060 is committed as16ef41f on codex/review-cache-01 in its
own worktree/build. No product source or owner worktree changed. The deliberately
red generated regression warms red root/a.png, navigates into sub/b.png, then
atomically replaces noncurrent a.png with blue while retaining4096 byte size
and mtime1700000000 UTC. previous() returns to a.png with cache-hit true and
red displayed pixels, while the source decodes blue. Windows targeted execution
is2 passed,1 failed,0 skipped,816ms,exit1; the assertion is the wrong displayed
pixel after successful setup/navigation. Report and raw output are
docs/verification/CACHE-01-CODEX-REVISION-REVIEW.md and
docs/verification/cache-01/noncurrent-same-size-time-red.txt in16ef41f.

Disposition is CHANGES REQUESTED for Cache revision validation. The review-only
test is not a green implementation to cherry-pick wholesale into main. Have the
Cache owner port it with the correction, preserve actual hits and lookahead,
then review the combined source and run full integrated gates. The prior Cache
approval did not exercise this case. FS source approval alone cannot waive it.
