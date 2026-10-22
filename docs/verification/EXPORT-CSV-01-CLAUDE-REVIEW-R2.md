# EXPORT-CSV-01 independent Claude-lane review, round 2

Verdict: **APPROVED** at pinned Gemini correction commit `25146f6450fcac5b7f6a8c6b71f47fe470665698` (on top of `d3df4ed`; resolves the four required corrections of [EXPORT-CSV-01-CLAUDE-REVIEW.md](EXPORT-CSV-01-CLAUDE-REVIEW.md)). Approval covers the engine on Windows (native MSVC) and Linux (WSL g++, not the Docker image). The full Windows gate and `docker build -t glancelot-browser:main .` were not run and remain the integrator's responsibility under the gate lock.

Reviewer: Claude lane (Sonnet 5.5), reviewer only. Worktree `.worktrees/claude-review-export-csv-01-r3`, branch `claude/review-export-csv-01-r3` created at `25146f6`, build `build-claude-review-export-csv-01-r3`. The only commits on the review branch are docs (the cherry-picked first review and this file); `git diff 25146f6 HEAD -- src tests include` is empty, so source and tests are exactly the pinned commit. `.worktrees/export-csv`, `docs/WORK-QUEUE.yaml`, shared contracts and CMake were not touched; no integration lock; no merge; no push. No UI is involved, so the ui-design-skills requirement does not apply.

Diff reviewed: `d3df4ed..25146f6`, two files only (`src/core/selection_export.cpp`, `tests/selection_export_tests.cpp`).

## Per-correction table

| # | Required correction | Fix (25146f6) | Test | Evidence | Result |
|---|---|---|---|---|---|
| 1 | Remove the filename-keyed branch; unconditional `progress(i,total)`; cancel tests with ordinary names | Branch gone. `selection_export.cpp:405` `progress(i, totalItems);` with no condition. Post-callback cancel check `:408`, loop-top check `:397`, pre-commit check `:577`. A grep of the source finds no `"cancel-"` string and no `destination.filename().string()` call | `selection_export_tests.cpp:431-461`: `cancel-single.csv` (cancel at `done==0`) and `cancel-multi.csv` (two items, cancel at `done+1==total`); both assert `Cancelled`, destination content `original`, `calls==1`/`calls==2` and `calls <= items`, and `countTempFiles(f.root)==0` | Probe P2a: a plain name cancelling at `done==total` returns Written with `lastDone=0`, i.e. that state is unreachable by design (progress is never reported at `done==total`), not a hole. P2b `status=1 calls=1 preserved=1 temp=0`; P2c `status=1 calls=2 preserved=1 temp=0`; P2d a file named `cancel-1.csv` first sees `done=0`, behaves like any other name. Mutation E (remove both the post-callback and the pre-commit cancel checks) fails both new cancel tests, so they discriminate | **Resolved** |
| 2 | Unicode destination and directory with a progress callback; no leftover temp; exception-safe cleanup | ASCII-only temp name `:317-321` (unchanged from d3df4ed); `destination.filename().string()` removed; RAII `TempCleanupGuard` `:330-344` removes the temp unless `committed` (`:643`) | `selection_export_tests.cpp:478-496` Unicode directory `目錄-📁` and file `測試-🖼.csv`, progress callback supplied; asserts `Written`, file exists, `progressCalls==1`, `countTempFiles(uniDir)==0`; runs on all platforms (the `#ifdef _WIN32` is gone) | Probe P1 `status=0 exists=1 temp=0` (was a throw plus a leaked temp). Mutation G (previous d3df4ed source with the new test) fails it: `Unicode destination must not throw: No mapping for the Unicode character...`. Exception safety demonstrated by probe P8: a lone-surrogate item name still throws from path to UTF-8 conversion (see O1) but `temp-after=0`, so the guard works. There is no dedicated unit test for the guard itself | **Resolved** |
| 3 | Real symlink/junction regression for the lexical relative-root policy | `:152` `nItem.lexically_relative(nRoot)` (unchanged from d3df4ed) | `selection_export_tests.cpp:498-530`: `create_directory_symlink(outside, root/"alias")`, falling back on Windows to `mklink /J`; item `root/alias/image.png`, `revalidate=false`; asserts `written==1`, no skip and output `alias/image.png`. If no link can be created the block is silently skipped | Mutation A (Windows, `fs::relative`): `FAIL: relative-root contract compares lexical paths across link`, `FAIL: output path is alias/image.png`, `FAILED with 2 error(s)`. The same mutation on Linux (WSL) fails the same two checks. So the test **actually ran** and discriminates on both machines I used | **Resolved** (see O2: the silent skip) |
| 4 | Hash-based dedup replacing per-pair filesystem calls; bounded-time test; mixed identity correct; case folding Windows-only | `FileIdentityHash` `:180-186`; `makePathKey` `:188-203` (lexically normal; `towlower` and `/` to `\` only under `#ifdef _WIN32`, plain UTF-8 on other platforms); three sets `:391-393`; rule `:444-456` (known: duplicate if identity seen or an *unknown* accepted item has the same path; unknown: duplicate if any accepted item has the same path); inserts `:559-563`. `arePathsEquivalent` is no longer in the item loop (only the linear destination-is-item scan before the loop) | Mixed identity: `selection_export_tests.cpp:422-429` (both orders). Bounded time: `:532-553`, 5000 unique plus 1000 duplicate unknown-identity items, `ms < 5000` | P6, Windows: n=2000 32.7 ms (was 91,069.8 ms); n=6000 91.7 ms (was 229,298 ms); n=100000 1,681 ms. Linux: 7.0 ms, 21.9 ms, 404.5 ms. P6b cancel at item 50000 of 100000: Cancelled, stop-to-return 22.0 ms (Linux 23.0 ms), no temp, no destination. Mutation G (d3df4ed O(n^2) source with the new test): `FAIL: 5000 items dedup completed in bounded time (took 121464 ms)`, 122 s, so the test fails against the old shape (limit 5000 ms against about 50 ms real: a wide margin). P9: mixed both orders `written=1 skipped=1`; hardlink known/known `1/1`; distinct known `2`; `sub/../source.png` normalised dedups `1`; case variant `SOURCE.PNG` vs `source.png`: Windows `written=1`, Linux `written=2`, so folding is Windows-only | **Resolved** |

## Regression and acceptance checks

- Destination race (finding 1 of round 1): still resolved. The test now also asserts the status (`:463-476`, `DestinationExists`) and no leftover temp. Mutation F (replace-allowed `MoveFileExW` plus the pre-check removed) fails with `status DestinationExists on late destination collision` and `overwrite=false must not replace a destination created during export`.
- Mixed-identity duplicates (finding 4 of round 1): still resolved in both orders (P9, test `:422-429`), Windows and Linux.
- Unbounded allocation: the three hash sets and the row buffer are bounded by `maxItems` (default 1,000,000, enforced before any work); path keys are one string per accepted item. No new unbounded growth. Ran 100,000 items without issue.
- Viewing never writes original media: only the destination and its temp sibling are written; item paths are only snapshotted (read-only). Not separately probed beyond code reading and the passing destination-is-item tests.
- Queue acceptance: RFC 4180 quoting, formula-injection neutralization, Unicode round-trip (item names and now destination file and directory), outside-root skip, revalidated snapshot, never overwriting an exported item, atomic temp-and-rename (Windows `MoveFileExW`, POSIX `link()` for no-replace and `rename` for overwrite), cancellation leaves nothing behind, bounded items and bounded work, generation echoed: all pass in the suite on Windows and Linux.

## Commands run and actual output

Build (core only, project-local toolchain): `scripts/build.ps1 -BuildDir build-claude-review-export-csv-01-r3 -CoreOnly` built `selection_export_tests.exe` and the core library without errors.

Targeted Windows ctest:

```text
ctest --test-dir build-claude-review-export-csv-01-r3 -R selection_export --output-on-failure -V
1: Running selection_export tests...
1: All selection_export tests PASSED!
1/1 Test #1: selection_export .................   Passed    0.37 sec
100% tests passed out of 1
Total Test time (real) =   0.45 sec
```

Reviewer probes, Windows (source and unedited output in `docs/verification/export-csv-01/review-25146f6-claude/`):

```text
P1 unicode-dest+dir+progress status=0 exists=1 temp=0
P2a cancel@done==total plain.csv status=0 (0=Written: the callback never reports done==total) calls=1 lastDone=0
P2b cancel@done==0 status=1 calls=1 preserved=1 temp=0
P2c cancel@last-item-start status=1 calls=2 preserved=1 temp=0
P2d dest=cancel-1.csv first progress done=0 (expect 0) status=0
P6 unknown-identity n=2000 status=0 written=2000 ms=32.6996
P6 unknown-identity n=6000 status=0 written=6000 ms=91.678
P6 unknown-identity n=100000 status=0 written=100000 ms=1681.2
P6b cancel@50000/100000 status=1 stop-to-return-ms=22.0272 total-ms=1520.44 temp=0 dest-exists=0
P9 mixed order=0 written=1 skipped=1 (expect 1/1)
P9 mixed order=1 written=1 skipped=1 (expect 1/1)
P9 case-variant unknown/unknown written=1 (Windows expect 1, Linux expect 2)
P9 dotdot-normalised unknown/unknown written=1 (expect 1)
P9 hardlink known/known written=1 skipped=1 (expect 1/1)
P9 distinct known written=2 (expect 2)
P8 lone-surrogate THREW: No mapping for the Unicode character exists in the target multi-byte code page. temp-after=0
```

Mutations (copies compiled in the scratchpad against the unmodified 25146f6 test file; the review worktree source is untouched):
- A, `lexically_relative` to `fs::relative`: `FAIL: relative-root contract compares lexical paths across link` / `FAIL: output path is alias/image.png` / `FAILED with 2 error(s)`.
- E, post-callback and pre-commit cancel checks disabled: `FAIL: single-item start cancellation preserves destination` / `FAIL: multi-item last item callback cancel preserves destination`.
- F, no-replace publication weakened: `FAIL: status DestinationExists on late destination collision` / `FAIL: overwrite=false must not replace a destination created during export`.
- G, d3df4ed source with the 25146f6 tests: `FAIL: Unicode destination must not throw: No mapping for the Unicode character exists in the target multi-byte code page.` / `FAIL: 5000 items dedup completed in bounded time (took 121464 ms)`, 122 s.

Linux (WSL Ubuntu 24.04, g++ 13.3, `-std=c++20 -O2 -Wall -Wextra -Wpedantic`; core sources compiled directly; not the Docker image): no diagnostics from `selection_export.cpp`; suite printed `Running selection_export tests...` / `All selection_export tests PASSED!`. The Linux probe (P1 and P8 excluded) printed the P2/P6/P9 results quoted above (including `P9 case-variant ... written=2` and `P6 n=100000 ms=404.548`). The Linux `fs::relative` mutation failed the same two link checks as on Windows.

## Non-blocking observations (no change required for approval)

- O1. Item paths that are not valid Unicode (a lone UTF-16 surrogate in an NTFS name) make the path to UTF-8 conversion throw `std::filesystem` / `No mapping for the Unicode character...` out of `exportSelection` (probe P8). The temp file is cleaned by the RAII guard, but an exception escaping a worker thread would terminate the process. This existed in the Codex-reviewed `7ded057` (`pathToUtf8`) and is not a regression. Consider catching `std::exception` around the item loop, returning `Failed` or skipping as `Unrepresentable`, in the later Qt service packet or a follow-up.
- O2. The link regression silently skips when no link can be created (for example without privilege and without `mklink`). It ran on this Windows machine (junction or symlink) and on WSL; consider emitting a visible "skipped" line.
- O3. Linux no-replace uses `link()`; filesystems without hard links would return `Failed` with no `renameat2(RENAME_NOREPLACE)` fallback (unchanged from round 1, unverified).
- O4. The bounded-time test is wall-clock based (5000 ms against about 50 ms real), which is robust here but remains time dependent.

## Unverified

- `docker build -t glancelot-browser:main .` and the Linux Docker image (the Linux result above is WSL g++ on the engine and test only).
- Full `scripts/build.ps1 -Test` gate and the integrated commit (integrator, under the gate lock).
- Behavior on filesystems without hard-link support (O3).

## Required corrections

None. The four round-1 corrections are resolved and each has a test that fails without its fix (mutations A, E, F, G). After this approval the lead synchronizes main, enables the root export target unconditionally under the shared-contract lock, and the integrator runs the full Windows gate and the Docker build through `scripts/gate-lock.ps1`, then merges with `--no-ff`. Gemini must not review its own fixes; this review is from the Claude lane.
