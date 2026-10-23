# F024 feasibility: visual similarity / near-duplicate groups for burst cleanup

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: DEFER** - the analysis is buildable with no new dependency (write our own dHash + DCT pHash over existing 256-edge thumbnails), but it sits on two things that do not exist yet (F023 exact duplicates in B7, F028 bursts in C5), so promote it to GO only after those are integrated. Size XL. Depends on: F023 (B7), F028 (C5), ASSET-GROUP (B1/B2), RESOURCE-LEDGER (E1).

## 1. What the feature needs in this codebase

Inventory row F024: "Visual similarity/near-duplicate groups for burst cleanup | C / P3 / XL | F023, local bounded analysis; show confidence and retain originals; no automatic destructive choice, model download or remote inference." Its stated dependency F023 is "Exact duplicate finder, scoped to chosen folders | N / P2 / L" and the neighbouring row F028 is "Burst/sequence stacks with user-selected representative | N / P2 / L".

There is **no requirement row** for F024 in `docs/REQUIREMENTS.md`. It is an inventory proposal only, so there are no acceptance criteria to preserve - the lead would have to add them through change control before any promise.

Existing code it would build on:

| Need | What exists | Where |
| --- | --- | --- |
| Pixels to hash | 2 persistent worker processes, edge 16..256, RGBA8888 converted to sRGB, `Qt::SmoothTransformation`, queue 256, 64 MiB memory-only cache, epoch fences | `include/glancelot/thumbnail_service.hpp`, `src/worker/main.cpp:46-153` (`--thumbnail-server`) |
| Bounded folder enumeration | `listDirectory(..., limit = 100000)`, worker-thread only | `include/glancelot/directory_listing.hpp` |
| Scan admission and priority | `JobPriority{Foreground, Thumbnail, Prefetch, Metadata}`, `tryAdmit(...)` byte ledger | `include/glancelot/io_scheduler.hpp` |
| Identity, so a group survives a rename | `FileIdentity{volume, file}`, `snapshotTarget()` | `include/glancelot/file_operations.hpp` |
| Review-only result presentation | planned stack UI from C5 (F028), planned ASSET-GROUP contract | `docs/handoff/LEAD-PLAN-2026-10-22.md` §6 |

New components this would add (none of them touch a shared header):

- `src/core/image_hash.{hpp,cpp}` - pure function: tightly packed RGBA8 + width/height -> `{std::uint64_t ahash, dhash, phash; std::array<std::uint8_t,32> blockMean;}`. No Qt types in the core path, no I/O.
- A new worker mode. The cheapest wiring is to extend the existing thumbnail-server JSON request with `"hash": true` so the worker returns the hash **instead of** the pixel block: 8-32 bytes on stdout rather than up to 256 KiB per image through `QSharedMemory`/stdout. For a 100k-image scan that is the difference between ~25 GiB and ~3 MiB of IPC.
- `src/core/similarity_index.{hpp,cpp}` - bounded candidate index plus grouping, cancellable, generation-stamped.
- A review-only controller + the C5 stack UI, with a confidence label per pair and no destructive default.

Owner: a new **Track C** packet (say C6), after B7 and C5. It must not be folded into C5: bursts are time-based and cheap, similarity is a decode-bound scan.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance (latest release + date) | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| **Write our own (Qt only)** | n/a | n/a | n/a | nothing to bundle | nothing to bundle |
| pHash (aetilius/pHash) | GPL-3.0-or-later [1] | Yes (same licence family as Glancelot) | 0.9.6 is the version phash.org lists; **no GitHub releases at all** [1][2] - treat as unmaintained | would need an MSVC build plus CImg/libjpeg; no upstream Windows support | no Ubuntu 24.04 package in the current Dockerfile |
| OpenCV `img_hash` (opencv_contrib) | Apache-2.0 [3][4] | Yes | OpenCV 5.0.0 (06 Jun) and 4.14.0 (19 Jul) [5]; `img_hash` lives in **opencv_contrib**, not core [6] | contrib module requires building OpenCV from source; tens of MiB of DLLs for six hash functions | same, in an image that today apt-installs no build libraries |
| CLIP / other embedding model | weights and runtime licences vary | needs case-by-case review | n/a | 150-600 MiB of weights in `dist\Glancelot.zip` plus an inference runtime | same in the container image |

Notes that decide this:

- `img_hash` offers average, block-mean, colour-moment, Marr-Hildreth, pHash and radial-variance - but **not dHash** [6]. Its own README says it is a port of the pHash library's algorithms [6]. Pulling OpenCV + opencv_contrib into a project whose only dependency is Qt, with no vcpkg, Conan or FetchContent anywhere in `CMakeLists.txt`, is not proportionate to six short functions.
- pHash's licence is actually fine for us (we are GPL-3.0-or-later), so the objection is maintenance, not law. No releases on the upstream GitHub is disqualifying for SYS-04 notices and for security response.
- I searched for a small permissive C/C++ perceptual-hash library and found none [7]. The algorithms are short and openly documented, and this project already writes its own bounded parsers (`src/app/raw_preview.cpp`, `src/app/metadata_parser.cpp`). **Write our own.**
- Embedding models: the product rule forbids model download and remote inference, so only a bundled local model would qualify. A bundled CLIP-class model adds hundreds of MiB to both the Windows zip and the container image, adds an inference runtime, and gives results that are not bit-reproducible across backends - which makes "show confidence" a number we cannot defend. **No-go for v1**, independent of licence.

### Algorithm choice

| Hash | Cost per image (after the thumbnail exists) | Robust to | Blind to |
| --- | --- | --- | --- |
| aHash 8x8 mean | trivial | re-encode, small resize | brightness/contrast shifts; very high false-positive rate on flat scenes |
| dHash 8x8 horizontal gradient, 64-bit | trivial | re-encode, resize, uniform brightness change | crop, rotation, subject motion |
| pHash 32x32 DCT-II, 8x8 low-frequency block, median threshold, 64-bit | ~32x32 DCT = a few thousand multiplies | re-encode, resize, gamma, mild blur | crop, rotation, large local motion |
| Block-mean 16x16, 256-bit | trivial | same as aHash, finer | same as aHash |

Recommendation: compute **dHash + pHash** (two 64-bit values) per image and report a pair as a candidate only when both are within threshold, with the *worse* of the two distances shown as the confidence. Bursts are near-identical frames, so this is an easy regime for global hashes - but subject motion inside a burst is exactly what makes global hashes diverge, which is why C5's time grouping must run first and the hash must only rank *within* a time group.

### Bounded index for N up to 100k

Two 64-bit hashes per image is 1.6 MB of hash data at N = 100k - the index is not the problem. For a Hamming threshold of `d <= 3`, split each 64-bit hash into 4 x 16-bit bands and keep 4 `unordered_multimap<uint16_t, index>` buckets; by the pigeonhole principle any pair within 3 bits agrees exactly on at least one band, so the candidate set is the union of 4 bucket lookups. That is ~4 x 100k entries, a few MB, no recursion, and a hard memory bound we can state. A BK-tree is the alternative and is also fine at this size, but it needs a recursion/visit budget to stay bounded, so banding is the simpler thing to defend. Either way the index must carry the scan generation and be dropped wholesale when the selection changes.

**The real cost is decode, not hashing.** 100k images through 2 worker processes at a conservative 25 ms per thumbnail decode is roughly 20 minutes of wall clock. So this is necessarily a scoped, cancellable, resumable background job with progress and partial results - the same shape F023 needs, which is another reason to build it after B7.

## 3. Platform capability

- **Windows** (MSVC 2022, Qt 6.8.3 under `.tools/Qt`): no new dependency, no new DLL in `dist\Glancelot`, no change to `scripts/build.ps1 -Package`.
- **Native Linux**: same; the hash path is plain C++20 and the thumbnail worker already runs there.
- **Docker/noVNC**: F024 is not one of the remote-mandatory features (F074/F076/F077/F078), but it works there with one caveat - `/photos` is bind-mounted **read-only** in `compose.yaml`, so a review-only result is the only possible outcome anyway, and any "resolve this group" action must be reported as unavailable rather than failing at write time. The container lacks `qtimageformats` until EN-02, so TIFF/WebP sources cannot be hashed there yet; the capability model (D2) must say so instead of silently producing smaller groups.

## 4. Security model

- Hash input is already-validated RGBA8 produced by the separate `glancelot-decoder` process (content-sniffed allowlist, dims <= 32768, <= 256 MiB decoded, `QImageReader::setAllocationLimit(256)`). The hash code itself never parses a file format, so no new parser boundary is created. The decoder still has no OS sandbox - that remains the project's stated limit, unchanged by this feature.
- Bound everything by construction: entries per scan (reuse the 100000 listing limit), index bytes, candidate pairs per group, groups per result. Reject rather than truncate silently, and say which.
- Never automatic: no destructive default, no pre-ticked "keep newest", no deletion from the similarity view without going through `file_operation_service` (trash) and - once B2 lands - the operation journal. Viewing and hashing never write original media.
- Thumbnails are never written to disk by contract; a persisted hash cache would break that spirit and would also be a mandatory-catalog smell. If a hash cache is wanted later it must be explicitly disposable and optional (E2 cache controls), never required for correctness.
- A group is a set of `FileIdentity` values plus paths; re-validate identity before any action, because a 20-minute scan guarantees the filesystem moved underneath it.

## 5. Test strategy

All fixtures generated at test time, nothing committed:

1. Paint a base image with `QPainter` (gradients plus a few shapes), save as PNG.
2. Derive: JPEG at quality 50; 50% downscale; 1-pixel translate; +10% brightness; 5% centre crop; 90-degree rotation; an unrelated base image.
3. Assert, as table-driven cases: re-encode/resize/translate/brightness stay within the chosen dHash and pHash thresholds; crop and rotation fall outside them (documented blind spots, asserted so a future change cannot quietly claim otherwise); unrelated images are far apart.
4. Identical bytes -> distance 0 for all four hashes.
5. Index property test: brute-force all pairs at N = 2000 random hashes and assert the banded index returns a superset of every pair within the threshold (no false negatives).
6. Failure and cancellation: unreadable file mid-scan (scan continues, file reported); worker killed mid-scan; `stop_token` during the scan leaves no groups published and no partial result presented as complete; a result whose generation is stale is dropped (same rule as `ThumbnailService`).
7. Memory: assert the index stays under its declared cap at N = 100k synthetic hashes (hashes only, no decode).

New test file `tests/similarity_ui_tests.cpp` plus a core test target in a lead-provided `cmake/SimilarityTests.cmake`; never added to `tests/grid_ui_tests.cpp`.

**Stays unverified / RUNBOOK:** real precision and recall on an actual photo library. We cannot ship user media, and generated fixtures cannot tell us whether the threshold is right for real bursts. That is a user-run RUNBOOK packet with the user's own folders, reporting counts only - and until it exists, no accuracy claim may appear in the UI or docs. Scan throughput on spinning disks and SMB is also RUNBOOK (PERF-WINDOWS/PERF-LINUX, F5).

## 6. Effort estimate

| Packet | Content | Size |
| --- | --- | --- |
| C6a | `image_hash` core (aHash/dHash/pHash/block-mean) + worker `"hash": true` request mode + tests | M |
| C6b | Banded index, grouping within C5 time groups, cancellable scan under `IoScheduler`/the E1 ledger, progress and partial results | L |
| C6c | Review-only UI: grouped stacks with confidence, keyboard review, explicit non-destructive resolution routed through existing services | L |

XL in total, Track C, three packets. Prerequisites: B7 (F023 - the scan skeleton, scoping and review-only result presentation should be shared, not duplicated), C5 (F028 - time grouping as the pre-filter), B1/B2 ASSET-GROUP and OPERATION-JOURNAL (so a group action is journaled), E1 RESOURCE-LEDGER (so a 20-minute scan is admitted against one budget rather than competing with the grid).

## 7. Recommendation and what would change it

**DEFER for v1.** The technical risk is low and there is no dependency decision to make - but the feature is the third layer on two unbuilt ones, it is the inventory's own C/P3/XL, and it has no requirement row. Building it now would mean duplicating F023's scan skeleton and F028's grouping UI and then reconciling them.

Promote to **GO** when all of these hold:

1. B7 is integrated and F023's folder-scoped, cancellable, review-only scan is reusable rather than private to the duplicate finder.
2. C5 is integrated and exposes time groups as a public grouping the similarity pass can narrow.
3. ASSET-GROUP is frozen and OPERATION-JOURNAL (B2) is in, so a resolution is journaled and undoable where supported.
4. The lead adds a requirement row with acceptance criteria, including the confidence label and the "no automatic destructive choice" rule.

Minimal viable slice, if the lead wants it earlier: **C6a only** - `image_hash` plus the worker hash mode plus the fixture-derived threshold tests, with no UI. That is a self-contained M packet, it is provably bounded, it gives the thresholds an evidence base, and it leaves the grouping and review surface to be built once on top of B7/C5.

What would make this NO-GO instead: if the lead decides similarity needs crop- and rotation-robustness. Global hashes cannot do that; the honest answer would be local features or an embedding model, and both are out of bounds for v1 (size, licence review, and non-reproducible confidence).

## Sources

All accessed 2026-10-22.

1. pHash repository (licence GPL-3.0-or-later; no releases listed) - https://github.com/aetilius/pHash
2. pHash download page, version 0.9.6 (via search; the page itself was not reachable from this session) - https://www.phash.org/download
3. OpenCV licence file (Apache-2.0) - https://raw.githubusercontent.com/opencv/opencv/4.x/LICENSE
4. opencv_contrib licence file (Apache-2.0) - https://raw.githubusercontent.com/opencv/opencv_contrib/4.x/LICENSE
5. OpenCV releases (5.0.0 on 06 Jun, 4.14.0 on 19 Jul; the page prints no year) - https://github.com/opencv/opencv/releases
6. opencv_contrib `img_hash` module and its public header (average, block-mean, colour-moment, Marr-Hildreth, pHash, radial-variance; README states it ports the pHash library) - https://github.com/opencv/opencv_contrib/tree/4.x/modules/img_hash and https://raw.githubusercontent.com/opencv/opencv_contrib/4.x/modules/img_hash/include/opencv2/img_hash.hpp
7. Search for a permissive C/C++ perceptual-hash library returned no suitable candidate; the BSD-licensed `dhash` found is Go - https://pkg.go.dev/github.com/andybalholm/dhash
