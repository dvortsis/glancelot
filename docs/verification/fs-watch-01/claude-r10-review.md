# FS-WATCH-01 independent Claude-lane review R10 (static only)

Candidate: `68c3731` (codex/fs-watch-01), reviewed in `claude-review-fs-watch-01-r10`. No test or build was run for this verdict.

**Verdict: not approved for integration at this head.**

- FS source `825f7f2` stays acceptable (R9, f0bf272: no blocker, no major).
- Commits after it are docs plus a merge of main (Cache engine). The lead's amendment 0212daa requires private FS source
  revisions to validate foreground cache hits and prefetch admission/publication. That correction is not in this head:
  `cacheKey()` is still path|size|mtime (viewer_controller.cpp:319), the navigation hit path (line 650) reuses
  pixels without a revision check, and prefetch admission (line 544) checks only `cache.contains`. The branch's only
  cache clearing is on observed FS changes, so a noncurrent same-size/restored-mtime replacement still serves wrong pixels
  (the case Codex review 16ef41f reproduced).
- The generated noncurrent-replacement regression the amendment asks for is not present (no new test since 825f7f2).

Needed before re-review: the narrow correction (capture revision before decode, revalidate before publish/admit, unknown
evidence is a miss, one running plus one pending validation, no per-asset watches), the regression test, and fresh Windows
and Docker gates on the merged head.
