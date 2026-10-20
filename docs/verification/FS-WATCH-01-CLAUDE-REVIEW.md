# FS-WATCH-01 independent Claude-lane review R8 (static only)

Candidate: f20711e (fixed), reviewed by the Claude lane (independent of the Codex implementing lane) in
`claude-review-fs-watch-01-r8`. Inspected diff: `git diff eff0aee f20711e`. The only source-touching files are
`src/app/directory_entry_model.cpp` (+3), `src/app/fs_revision.hpp` (1 line) and `tests/fs_watch_tests.cpp` (+42/-2);
the rest is docs/evidence (FS-WATCH-01.md, my preserved R7 report, `windows-r9-active-filter.txt`). I read the full touched
functions (`pump` finished handler, `apply`, `applyFilter`) and the whole 100k test. Prior R7 report
(`claude-r7-review.md`) is preserved verbatim.

**Static review only.** I started no Windows test, Docker build, native app or compile job and edited no product,
test, interface, CMake or queue file. I have no runtime evidence of my own for f20711e; everything below is from reading
source and the author's output. The candidate is still f708068 contracts plus FS; the Cache owner code now on main
(8b28060) is not imported, and Cache synchronization/compatibility remains a future integration gate.

## Disposition split
- **Implementation/source disposition:** no blocking finding in the f20711e delta. Product change is test-only
  instrumentation under `GLANCELOT_FS_WATCH_TESTING`. In production builds `ModelPublicationStats` and both statements
  compile out, so there is no behavioural, ABI or public-header change.
- **Final packet approval: NOT GIVEN.** Final packet is not done; the gates under "Pending" are not waived.

## What I checked
- `applied`/`filtered` are written at the top of `apply()`. The handler resets `publicationStats={}` first, so stale
  (`token != generation || !active`) and cancelled results keep `applied=false`. Stale results therefore cannot satisfy
  either test predicate, and the existing `admittedWithoutPrefix` check is tightened by `value.applied`. Stale results
  are still counted in the unchanged all-publication `<250ms` loop, so discarded-listing disposal on the GUI thread is
  still covered by the budget.
- The filtered branch really is the costly path: `rawListing=std::move(listing); applyFilter()` is a full model reset,
  per-row match and row copy (peak ~2x rows, bounded by kMaxEntries). The 100k test hits it: evidence shows
  filtered=true publications with merge 0 and projection 53-62 ms.
- Real monitor-driven: the writer is a separate `std::jthread` doing real `QSaveFile` atomic replacements. No
  `refresh()` or simulated invalidation is called in the filtered phase, so publications come from the monitor path.
- Oracles unchanged: 250 ms initial gap, publication, sustained gap and 30 s `QTRY` recovery. New 250 ms bounds on
  filter change, filtered publication and filtered gap. The last-complete-baseline assertion (`observed 0 / accepted 1`)
  and the final rename-follow assertions (selection, cursor, hash) are retained, run after `clearFilter()`, and the
  loop at the end still asserts every publication including filtered ones `<250ms`.
- Async semantics unchanged: generation/token check, single consumer `takeResult`, bounded `kMaxConcurrentListings`,
  and no new queue or allocation. Peak memory in the filtered phase (raw + visible copy) is bounded by the existing cap.
- Author output (not my execution): 3 passed, 0 skipped, 72.495 s; filter change 29 ms, filtered publications 62/53 ms,
  filtered gap 63 ms; unfiltered max 135 ms. Overlapped another lane's UI suite, so not exclusive-host evidence;
  consistent with the author's disclosure.

## Findings
No blocker, no major.

**Minor**
- **G4 (new, test precision):** `filteredSize` is `len("N filtered generated changed bytes")`, which depends only on the
  digit count of N. The final `QTRY` therefore passes on any filtered publication after write 10, not necessarily the
  last write, and does not prove convergence to final content. Docs say "await the final metadata size"; that overstates it.
  It still proves at least one filtered publication after the filter was enabled (separately asserted by `filteredPublished`).
  Fix, if wanted: encode a unique fixed-width suffix or compare the final write index in the size/mtime.
- **G5 (new, naming/measurement scope):** `applied` is set before the `!truncated || ...` guard, so it means "listing
  reached apply()", not "projection ran". Safe for this test (100k is under the cap, so the projection runs), but misleading
  if reused. Also the 250 ms filter oracles measure model reset only, not QML GridView delegate creation or relayout; the
  test has no view. G1's active-filter model cost is now measured, but the real view cost under filter is unmeasured.
  The filtered refresh still resets the whole model (scroll/delegates lost) instead of merging: inherited behaviour, not a regression.
- **Sample size:** only 2 filtered publications (62, 53 ms) under 70 writes because the monitor coalesces; one run on a
  loaded host. That is evidence of bounded cost, not a margin proof. Merge-path max was 135 ms in the same run, so the
  250 ms budget has ~115 ms headroom there.
- **G2 (not waived):** final rename-follow assertions were still never shown red against a baseline-replacing mutation.
  Does not block: the budget assertion that guards the same mechanism was shown red by the author, and the R7 52/52 passed.
- **G3 (not waived):** stale/cancelled `Listing` is destroyed on the GUI thread. Does not block: bounded by the row cap and
  inside the measured, asserted-`<250ms` publication window.
- Bound, not a bug: ~35k+ entries per folder get no rename tracking (16 MiB identity budget).

## Pending final gates (nothing here was run by me)
1. Full current-source Windows `scripts/build.ps1 -Test` (25 targets), exclusive host, owned by the integrator.
2. Full `docker build -t glancelot-browser:main .` on the integrated source.
3. Native F5 runtime in preview/grid/combined with text-input and settings-modal precedence (Linux software evidence from R7 is not native).
4. Serial integration under the lock after merging current main (with Cache owner code), then checks on the integrated commit,
   including Cache revision/invalidation synchronization.
5. Unverified by anyone: physical/network/coarse-timestamp/ReFS/cloud filesystems, registration ABA, GPU hardware.

The targeted 3/3 and my static reading are not full-packet approval.
