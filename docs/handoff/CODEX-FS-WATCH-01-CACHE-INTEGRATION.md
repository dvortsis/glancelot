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
