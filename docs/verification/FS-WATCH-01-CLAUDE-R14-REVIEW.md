# FS-WATCH-01 Claude R14 review

Candidate: `4a8652883d6d5a68b41340ce6639c201ee15ce32` (branch `claude/review-fs-watch-r14`), diff `2414cd6..4a86528`.
Reviewer: Claude (independent of the Codex implementing lane).

## Methods actually used

Source and git inspection only: `git diff 2414cd6..4a86528` (source, tests, docs), the full `merge()`/`updateMetadata()`/`reset()`/`publish` region of `src/app/directory_entry_model.cpp`, the `Row`/`Listing` definitions, `docs/verification/FS-WATCH-01.md`, and the retained `windows-main-synced-first-{full,budget}.txt`. I did not open the 512-line `-cases.txt` beyond confirming it exists in the diff. **No build, test, Docker, launcher or Defender action was run. Nothing here is measured by me.** No hardware or Linux evidence was available or examined.

## Change summary

1. `merge()` gains a fast path: when `rows.size()==incoming.size()` and every `key` matches position by position (`std::equal`), it records `mergeScanMs`, calls `updateMetadata(listing)` and returns.
2. The old trailing "metadata may have changed" block is extracted verbatim into `updateMetadata()`, used by both paths.
3. New test `metadataOnlyRefreshPreservesPersistentRowsAndSelection`.

## Correctness analysis

- **Equivalence.** With identical key sequences, the fallback would find all rows survivors, in order, zero runs, no removals or insertions, and reach the same metadata block. The fast path is therefore behaviourally identical for that input class, minus redundant hashing.
- **Changed metadata/type.** `Row::operator==` is defaulted over path, name, key, kind, link, hidden, size and modified. A kind change (file to dir) or size/mtime change makes rows differ, they are copied, and a single `dataChanged(first,last)` spans the range with no role list (all roles). Unchanged.
- **Index.** `index = std::move(listing.index)`: with equal key sequences the new index maps the same keys to the same rows, so replacement is consistent. Duplicate keys would be a pre-existing property of `buildListing`, not changed here.
- **Reordered keys / insert / remove / rename.** Any size or positional key difference fails `std::equal` and takes the unchanged fallback (survivor order check, run cap, reset). A pure reorder with the same set fails the fast path and resets as before. A same-count rename (b to c) mismatches and falls back. Correct.
- **Reentrancy/signals.** `rows` and `index` are fully updated before `dataChanged` is emitted, exactly as before. No begin/end pairs on this path, so no new model-signal imbalance. The caller still mirrors `rows` and `index` into `rawListing` afterwards and emits `selectionChanged`. Persistent indexes stay valid because there is no reset or row move.
- **Empty guard.** `rows.empty()||incoming.empty()` still resets before the fast path; the fast path never runs on empty input.
- **Listing lifetime.** `listing` is taken by value in `merge`; `updateMetadata` takes `Listing&` and only moves its `index`. Rows are copied, not moved, so the disposed listing destructs as before. No dangling reference: `incoming` is a reference into the local `listing`, valid for the call.
- **Stale results / generation.** Untouched; gating happens before `merge`.

## Findings (prioritized)

No correctness defect found in the changed source.

1. **Medium, evidence gap: performance benefit is unmeasured and may be insufficient.** In the failed run the 358 ms publication was `merge 326` of which `merge scan 227`, `projection/raw mirror 31`. The fast path removes only the scan (up to 227 ms there). The remaining roughly 100 ms of merge cost is row comparison and assignment across 100k rows, `index` move, and disposal of the old index and listing, all unchanged, plus the `rawListing.rows = rows; rawListing.index = index` full copy (about 9 to 31 ms measured). The same run's first publication had scan 13 ms and total 54 ms while later ones had 177 and 227 ms scans, which suggests timing variance and contention (the doc itself records a concurrent Windows/Docker suite), so a hash-scan reduction cannot be assumed to bring the maximum under 250 ms. Do not claim improvement until a fresh Windows `fs_watch_budget` run passes under the unchanged limits.
2. **Low, test does not discriminate the new path.** The regression uses 2 rows, and the pre-change fallback also preserves persistent indexes, selection and emits `dataChanged` with no reset, so this test would pass on `2414cd6` too. It guards against regressions but does not prove the fast path was taken. Missing cases: kind change (file to directory) with `dataChanged`, same-count rename or reorder falling back correctly, and a stat hook such as a test counter for fast-path hits. Suggest a follow-up, not a blocker.
3. **Low, observability.** `mergeScanMs` is now set on the fast path after a cheap compare; its meaning changes between paths, which complicates comparing logs across candidates. Note this when reading new numbers.
4. **Info, scope.** Dense 100k/250 ms/30 s recovery, backend/budget 180 s and UI 90 s limits are unchanged in this diff. The grid 300 s timeout and `cache` failure in the failed run are not addressed by this change and remain open and uninvestigated; this optimization says nothing about them. CACHE-01 and IO-01 aggregate-resource, eviction-pressure and physical-storage acceptance are not covered or claimed.

## Does the optimization preserve required behaviour?

By source reasoning, yes: the fast path is a strict shortcut to a state the fallback already reached for identical key sequences, with unchanged signal, index and selection semantics, and all non-identical cases still use the fallback. This is a static conclusion; it has not been exercised by me or yet by the new test on any platform.

## Disposition

Source: **accept as a narrow, behaviour-preserving optimization, no changes required** (finding 2 is an optional hardening). Acceptance status of FS-WATCH-01: **still open**. The prior `72f0afa` Windows 32/32 and Linux 32/32 gates cannot qualify this commit, and the `2414cd6` 29/32 failure is not green. Required before closing: fresh Windows full suite with the dense, grid and cache tests passing under unchanged limits and exclusive host load, a fresh Linux/Docker run, and the integrated-commit gates on the final merge. Any measured result must be reported as is; a failure should not be attributed to the environment without isolation evidence.
