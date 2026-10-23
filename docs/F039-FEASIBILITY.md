# F039 feasibility: archive / comic / continuous-strip browsing

EN-09 (track session S4, Phase 0 cycle 1). Base `afeac1a`. Written 2026-10-22. A proposal for the lead: nothing here is a support claim or delivery evidence.

**Recommendation: GO for two separable slices, DEFER the rest.** Headline: **GO** for (A) continuous strip + dual-page spreads over ordinary folders, Qt-only, no new dependency, and (B) a read-only ZIP/CBZ virtual source behind a new frozen SOURCE-PROVIDER contract, using libarchive in the decoder worker; **DEFER** RAR/CBR, 7z, encrypted and solid archives, and nested archives. Size: A is L, B is XL. Depends on: SOURCE-PROVIDER (new, lead-frozen), DECODE-TIER (D1), RESOURCE-LEDGER (E1), a DEP-DECISIONS entry for libarchive.

## 1. What the feature needs in this codebase

Inventory row F039: "Archive/comic/continuous-strip browsing | E ADV-02, ADV-10, ADV-11 / P3 / XL | Virtual source provider; bounded decompression, no archive extraction while viewing, correct cover/LTR/RTL layout and sparse visible working set."

Requirement rows (all expansion milestone E1):

- ADV-02 "Archives behave as virtual folders without disk extraction" - "ZIP/CBZ first then qualified families; deterministic traversal, entry cancellation, bounded decompression, no archive payload temp files."
- ADV-10 "Continuous vertical strip" - "Virtualized image/layout working set; padding control, 10,000-item scrolling, tall-image and cancellation tests."
- ADV-11 "Dual-page spreads, LTR/RTL and cover offset" - "Odd/even counts, cover toggles, direction changes and folder/archive boundaries preserve correct ordering."
- FMT-12 ".zip, .cbz, .rar, .cbr, .7z, .tar" - "Encryption/solid archives/seek behavior and limits; no extraction to disk; archive revision invalidates cached entries."

ADV-02 itself says "ZIP/CBZ first then qualified families", and ADV-10/ADV-11 are pure layout requirements whose acceptance criteria mention archives only as a boundary case. **That is the split the requirements already sanction.**

### The structural obstacle

Every identity in the codebase is a real filesystem path:

- `include/glancelot/contracts.hpp`: `struct AssetId { std::filesystem::path path; }`; `NavigationSnapshot{root, directory, current, files}` is a vector of paths.
- `include/glancelot/directory_listing.hpp`: `listDirectory(const std::filesystem::path&, ...)` - real filesystem only. There is **no virtual source provider abstraction**.
- `include/glancelot/navigation.hpp`: `imageFiles`, `sortedImageFiles`, `childFolders`, `firstImageInSubtree` all take and return `std::filesystem::path`.
- `src/worker/main.cpp:161-163`: the decoder process takes **exactly one path on argv** (`if (app.arguments().size() != 2) return fail("Expected one image path.")`) and probes the source revision itself via `fs_watch::probe(fs_watch::native(path))`.
- `include/glancelot/thumbnail_service.hpp` / the `--thumbnail-server` protocol: `{"id":N,"path":"...","edge":E}` - again a path.
- `include/glancelot/file_operations.hpp`: `MutationTarget{path, kind, link, identity, ...}` with `FileIdentity{volume, file}`. An archive entry has **no file identity**, so every mutation path must explicitly refuse it rather than treat the entry string as a path.

So slice B's real work is a new lead-frozen contract, not decompression:

- `include/glancelot/source_provider.hpp` (**SOURCE-PROVIDER**): an opaque `SourceId{std::filesystem::path container; std::string entry;}` with a canonical display form, a `listSource()` returning the same `DirectoryListing` shape, an `openEntry()` that yields bytes in bounded chunks, and a `sourceRevision()`. An empty `entry` must mean exactly today's filesystem behaviour so existing call sites keep compiling and behaving.
- `AssetId` gains an `entry` field (defaulted empty). That is a shared-header change and therefore lead-only, in a frozen contract commit.
- Decoder worker gains an argv form for container+entry (or a persistent `--archive-server` mode). **The decompressor must live in the worker process**, not in the UI process: handing the parent the job of inflating an entry and passing pixels in would put an archive parser inside the GUI process and break the SYS-02 parser-boundary rule. The worker reads the container itself, decompresses the one entry into a capped buffer, and decodes from memory - `QImageReader` already works on a `QBuffer`, which `src/app/raw_preview.cpp`'s embedded-JPEG path (`src/worker/main.cpp:176-186`) already demonstrates.
- Cache keys: `fs_watch::probe` on the **container** gives the archive revision for free (size + mtime). Every decoded/thumbnail cache key becomes (container revision, entry, tier). FMT-12 requires exactly this.
- Mutation refusal: `mutation_service`, `copy_move_service` and `file_operation_service` must each reject a `SourceId` with a non-empty entry with a named reason. This is the single most dangerous part of the slice and deserves its own tests.

Owner: a new track (propose **Track G, archives and reading layout**), or Track D for slice B's decode plumbing if the lead prefers to keep decoder-worker changes in one place. Slice A can be a Track A packet; it only touches layout and needs no contract.

## 2. Candidate dependencies

| Candidate | Licence | GPL-3.0-or-later compatible? | Maintenance (latest release + date) | Windows bundling | Docker bundling |
| --- | --- | --- | --- | --- | --- |
| **libarchive** | BSD-2-Clause for the C sources, with named exceptions: 3-clause UC Regents parts in two filter files and `mtree.5`, one public-domain file, BLAKE2 files CC0/OpenSSL/Apache-2.0 at our choice [1] | Yes | 3.8.10, 14 Oct (GitHub prints no year; the current `master` header declares `ARCHIVE_VERSION_NUMBER 3009000`) [2][3] | build from source with MSVC; one extra DLL; keep every external library off except zlib (no bzip2/lzma/zstd/xml2/openssl/b2) and the footprint stays small | **build 3.8.10 from source** in the `build` stage with the same minimal options (DEP-11); not Ubuntu's package (noble's runtime package is `libarchive13t64` 3.7.2, which links acl, bz2, lz4, lzma, nettle, xml2 and zstd) [15][16] |
| **minizip-ng** | zlib licence [4] | Yes | 4.2.2, 30 Jun [5] | build from source; needs zlib | not in the image today |
| **miniz** | MIT [6] | Yes | 3.1.2, 01 Jul [7] | single C file vendored into the tree; nothing to link | same |
| Qt private `QZipReader` | LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only (Qt) [8] | Yes | ships with Qt 6.8.3 | nothing to bundle | nothing to bundle |
| UnRAR source (RARLAB) | UnRAR licence - use-restricted | **No.** Fedora classifies it GPL-incompatible and non-free [9] | n/a | n/a | n/a |
| 7-Zip / LZMA SDK | public domain (since 4.62); SPDX notes it is compatible with GPL/LGPL code [10] | Yes | 26.03 dated 2026-09-03 on the `d.7-zip.org` mirror, 26.01 dated 2026-04-27 on the main page - **unconfirmed which is current** [10] | vendored C sources | same |
| **Write our own ZIP reader** | n/a | n/a | n/a | needs an inflate implementation (see below) | same |

Decisions this table forces:

- **`QZipReader` is not usable for this.** It is QtCore *private* API in Qt 6 (`src/corelib/io/qzipreader_p.h`, "This file is not part of the Qt API... may change from version to version without notice, or even be removed. We mean it.") [8], and a May 2026 Qt development thread proposes either removing it or making it public [11] - so the removal risk is live, not theoretical. Worse, its only entry accessor is `QByteArray fileData(const QString&) const` [8]: the whole entry is inflated into memory in one call, with no streaming and no way to stop at a byte cap. Bounded decompression (ADV-02) cannot be implemented on top of it.
- **Write-our-own needs a deflate implementation we do not have.** Qt bundles zlib privately but exports no public inflate API; `qUncompress` consumes zlib-wrapped data with a 4-byte length prefix, not the raw deflate streams inside a ZIP. So "write our own" really means "vendor miniz or zlib and write the central-directory parser on top". That is entirely feasible - the project already ships hand-written bounded parsers (`src/app/raw_preview.cpp`, `src/app/metadata_parser.cpp`) - and miniz is a single MIT file. It is the right fallback if the lead rejects libarchive, and it covers stored + deflate, which is what real CBZ files use.
- **libarchive is the better primary.** Its API is already the shape bounded decompression needs: `archive_read_set_seek_callback` for seekable containers, `archive_read_data_block` for chunked streaming reads we can cap and cancel, `archive_read_add_passphrase` / `archive_read_set_passphrase_callback` for detecting encrypted entries, and per-format opt-in (`archive_read_support_format_zip`, `..._7zip`, `..._rar5`) so we enable only what we qualify [3]. It reads ZIP, 7-Zip, tar and both RAR and RAR v5 [12].
- **libarchive avoids the UnRAR licence problem entirely.** Its RAR readers are independent BSD-2-Clause implementations (`archive_read_support_format_rar.c`, Tim Kientzle and Andres Mejia; `archive_read_support_format_rar5.c`, Grzegorz Antoniak) [13] - no RARLAB code. So RAR is a *qualification* question, not a licence question. That corrects the assumption in the packet brief.
- **Licence elections to record.** libarchive's CMake compiles every format source unconditionally (formats are enabled at run time), and with `ENABLE_LIBB2=OFF` it compiles its bundled BLAKE2 reference files, which RAR5 uses, even though RAR5 is never registered. Those files are offered under CC0, OpenSSL or Apache-2.0; record **CC0-1.0** (Apache-2.0 also acceptable; the OpenSSL-licence option is not GPL-compatible) in THIRD_PARTY_NOTICES (DEP-11).
- Note that most real 7z archives need LZMA/LZMA2, so enabling 7z means adding liblzma to both platforms; that is a second dependency decision and another reason to defer 7z.

## 3. Platform capability

- **Windows** (MSVC 2022, Qt project-local under `.tools/Qt`): libarchive builds with CMake and MSVC. The supply route is DEP-16's (FetchContent with a SHA-256-pinned official tarball first, vcpkg later), configured as DEP-11 says: every external library off except zlib, and only the ZIP reader registered at run time. Windows has no system zlib and Qt's is private, so slice B also adopts a pinned **zlib 1.3.2** (Zlib licence, SHA-256-pinned tarball, static, linked into `archive.dll` and shared with libjpeg-turbo if both land; DEP-11): two new libraries on Windows, not one. `scripts/build.ps1 -Package` must copy the new DLL into `dist\Glancelot` and `THIRD_PARTY_NOTICES.md` must gain the libarchive notice plus its named exceptions and the BLAKE2 licence election, and a zlib notice with `licenses/zlib/` (SYS-04; see below).
- **Native Linux**: the same pinned source and the same options as Windows and Docker (DEP-DECISIONS cross-cutting rule 3). Ubuntu's `libarchive-dev` is acceptable only as a developer convenience, never for a build that is tested or shipped.
- **Docker/noVNC**: F039 is not one of the remote-mandatory features, but it must not regress the container. Today the `build` stage apt-installs no archive library and the `runtime` stage installs only noVNC/TigerVNC/openbox/tini and copies `/opt/Qt`. Per DEP-11, the `build` stage compiles libarchive 3.8.10 from source with the minimal options and the `runtime` stage copies the resulting library (as it copies `/opt/Qt`); no apt archive package is added to either stage. `/photos` is bind-mounted **read-only**, which suits a read-only archive source perfectly. The container also lacks `qtimageformats` until EN-02, so a CBZ full of WebP pages would list but not decode there - the capability model (D2) must say so.
- **Slice A (strip + spreads) needs nothing on any platform**: it is QML layout plus the existing virtualized filmstrip/grid machinery, and it works identically in the container.

## 4. Security model

Archive formats are a classic attack surface, and libarchive has a long CVE history (recent Ubuntu advisories cover a heap out-of-bounds read in RAR handling and a flaw in the RAR5 decoder [14]). That history argues *for* the design the project already has - the parser runs in `glancelot-decoder`, a separate process - and *against* enabling RAR at all in v1.

Required controls:

| Threat | Control |
| --- | --- |
| Zip bomb / declared-size lie | Cap on **actual** bytes written per entry (reuse the 256 MiB decoded ceiling, and a smaller compressed-input ceiling), a compression-ratio ceiling, and a per-archive total. Enforced on the output of `archive_read_data_block`, never on the header's declared size. |
| Entry-count bomb | Listing cap mirroring `listDirectory`'s `limit = 100000`, reported as `truncated`, never silently cut. |
| Path traversal / hostile names | No extraction to disk, so traversal has no target - but entry names must be treated as **data**: never concatenated into a filesystem path, never used as a cache filename, control characters and bidi overrides neutralised for display, and the same "unrepresentable" rejection style `selection_export.hpp` already uses for CR/LF in paths. |
| Encrypted entries | Detect and report as unavailable. No passphrase prompt in v1 (that is a UI and a secret-handling decision of its own). |
| Solid / sequential-only archives | 7z and RAR solid blocks have no random access: jumping to page 400 may require decompressing pages 1-399. Either refuse the family or force strictly sequential prefetch with a visible label. This is the main reason 7z/RAR are deferred, independent of licence. |
| Nested archives | Refuse. One level only. |
| Container changed under us | `fs_watch::probe` the container before and after reading an entry, exactly as `src/worker/main.cpp` already does for single files, and invalidate every cached entry on a revision change (FMT-12). |
| Accidental mutation of an archive | `mutation_service`, `copy_move_service` and `file_operation_service` must reject any `SourceId` with a non-empty entry, by contract and by test. Viewing never writes original media; an archive is one file and must stay byte-identical. |
| Memory exhaustion in the worker | Keep `QImageReader::setAllocationLimit(256)`; add the decompression buffer to the same admission ledger (E1) rather than running a second uncounted budget. |
| Parser bugs | Register the archive reader as a libFuzzer/ASan target in the Linux build (Track F4), alongside the existing metadata/IPC/config targets. |

Authorization boundary: there is no remote feature here. In the container the archive is reached through the read-only `/photos` mount, so the host's own permissions remain the boundary.

## 5. Test strategy

Fixtures generated at test time, nothing committed (no real CBR/CBZ files - we have no licence to redistribute anyone's comics, and generated media is the project rule):

- Generate ZIP bytes directly in the test, without libarchive's write API, so the library under test is exactly the read-only configuration that ships (DEP-11) and there is no shell `zip` dependency and no committed binary. A small test helper writes local headers, the central directory and the end record; stored entries copy bytes, deflated entries use `qCompress` output with the 4-byte length prefix, the 2-byte zlib header and the 4-byte Adler-32 trailer removed (raw deflate), and CRC-32 is a few lines of table code. Cases: single-page, 1000-page, mixed image/non-image entries, folders inside the archive, empty archive, unsorted entry order (to prove deterministic traversal), non-ASCII and RTL entry names, an entry name containing a path-traversal sequence, a stored (uncompressed) entry and a deflated one.
- Bomb fixture: one entry of ~1 GiB of zeros, which deflates to roughly a megabyte, generated at test time by streaming raw deflate (`windowBits = -15`) through the same pinned static zlib that libarchive links (the test target links it directly; libarchive does not export zlib symbols; DEP-11), so the test never holds the 1 GiB input (`qCompress` would); alternatively run the bomb case against a smaller test-configured cap. Assert the read stops at the cap with a named reason and allocates no more than the cap.
- Truncated/corrupt archive: take a valid ZIP and chop it; assert a clean failure, no crash, no partial page presented as complete.
- Encrypted entry: assert it is listed and reported unavailable, not attempted.
- Cancellation: `stop_token` during a long entry read; worker killed mid-read; assert no temporary files anywhere (walk the temp directory) and no partial image shown.
- Revision: rewrite the container between two page requests; assert cached entries are dropped and the stale result never replaces the newer selection (generation rule).
- Mutation refusal: attempt delete/rename/copy/move/trash on an archive entry through each service; assert refusal with a reason, and assert the container file's bytes are unchanged (hash before/after).
- Slice A, in a new `tests/reading_layout_ui_tests.cpp` with a lead-provided `cmake/ReadingLayoutTests.cmake`: 10,000-item strip scrolling with a bounded visible working set; a very tall image; padding control; spreads with odd and even page counts; cover toggle on and off; LTR and RTL; direction change mid-folder; the folder/archive boundary preserving order (ADV-11's exact criteria).

**Stays unverified / RUNBOOK:** real-world CBZ and CBR corpora; 7z and RAR5 correctness breadth; scrolling smoothness on physical hardware at 60/144 Hz (that belongs to D3/F5); throughput on SMB shares. No headless result may be relabelled as physical qualification.

## 6. Effort estimate

Slice A - **GO now, L**, one or two packets, Track A:

| Packet | Content | Size |
| --- | --- | --- |
| A-strip | Continuous vertical strip: virtualized layout, padding control, tall-image handling, 10,000-item scroll test, cancellation (ADV-10) | M |
| A-spreads | Dual-page spreads, cover offset, LTR/RTL, boundary ordering (ADV-11) | M |

Slice B - **XL**, four packets, after the contract freeze:

| Packet | Content | Size |
| --- | --- | --- |
| G1 | SOURCE-PROVIDER contract + `AssetId::entry` + libarchive and the pinned zlib in the build on both platforms + notices (lead-owned files; the lead freezes this) | M |
| G2 | ZIP/CBZ provider in the worker: bounded listing, chunked capped entry read, container revision, decode from memory, thumbnail-server entry support | L |
| G3 | UI plumbing: archives as virtual folders in tree/grid/filmstrip/navigation, deterministic traversal, cache keys, **mutation refusal everywhere** | L |
| G4 | Capability and recovery surface: encrypted/solid/truncated reporting through D2's capability model and E4's recovery UX | M |

Prerequisites: DECODE-TIER (D1) - otherwise every page request is a full decode; RESOURCE-LEDGER (E1) - the decompression buffer must be admitted against the same budget; D2 capability model - so unsupported families are stated rather than inferred; a DEP-DECISIONS entry for libarchive (and, if slice B's fallback is chosen instead, for miniz).

## 7. Recommendation and what would change it

**GO for slice A now.** It needs no dependency, no contract and no decoder change; it satisfies ADV-10 and ADV-11 against ordinary folders, which is where most of the user value is; and it de-risks slice B by proving the layout before the I/O lands.

**GO for slice B, ZIP/CBZ only, after the contract freeze.** libarchive is BSD-2-Clause with documented exceptions [1], actively released (3.8.10, 14 Oct [2]), has exactly the bounded-streaming and seek API this needs [3], and its RAR readers are its own BSD code rather than RARLAB's [13]. The dependency is justified by the alternative: `QZipReader` cannot be bounded and may be removed [8][11], and rolling our own means vendoring miniz plus writing a central-directory parser anyway.

**DEFER** RAR/CBR, 7z, encrypted entries, solid archives and nested archives. For RAR and 7z the blocker is random-access cost and parser risk, not licence. For tar, note it is streaming-only by nature - a tar of 500 pages has no seekable index, so it belongs with the deferred families even though it is trivially readable.

**NO-GO, permanently:** the UnRAR source. Fedora classifies its licence as GPL-incompatible and non-free [9], and Glancelot is GPL-3.0-or-later. If RAR is ever wanted, it comes through libarchive's own implementation.

What would change the recommendation:

- If the lead will not accept the project's first non-Qt runtime dependency, slice B becomes "vendor miniz (MIT, single file, 3.1.2 [6][7]) and write a bounded ZIP central-directory reader". Add roughly one packet of effort and accept stored+deflate only. Slice A is unaffected.
- If `QZipReader` is made public API in a Qt version the project adopts, re-evaluate - but only if a streaming, cappable entry accessor comes with it; `fileData()` alone is not enough.
- If a RUNBOOK on real CBR files shows users need RAR before anything else, the question becomes whether libarchive's RAR5 reader is trustworthy enough to run even in the worker process; that is a fuzzing result (F4), not a judgement call.

## Sources

All accessed 2026-10-22.

1. libarchive `COPYING` - BSD-2-Clause for the C sources, with the 3-clause UC Regents, public-domain and BLAKE2 exceptions named - https://raw.githubusercontent.com/libarchive/libarchive/master/COPYING
2. libarchive releases, newest v3.8.10 dated 14 Oct (the page prints no year) - https://github.com/libarchive/libarchive/releases
3. libarchive `archive.h` - `archive_read_set_seek_callback`, `archive_read_data_block`, `archive_read_support_format_7zip`, `archive_read_support_format_rar5`, `archive_read_add_passphrase`, `ARCHIVE_VERSION_NUMBER 3009000` - https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive.h
4. minizip-ng licence (same terms as zlib) - https://raw.githubusercontent.com/zlib-ng/minizip-ng/develop/LICENSE
5. minizip-ng releases, newest 4.2.2 dated 30 Jun - https://github.com/zlib-ng/minizip-ng/releases
6. miniz licence (MIT) - https://raw.githubusercontent.com/richgel999/miniz/master/LICENSE
7. miniz releases, newest 3.1.2 dated 01 Jul - https://github.com/richgel999/miniz/releases
8. Qt 6.8 `qzipreader_p.h` - QtCore private, "not part of the Qt API... may even be removed", `QByteArray fileData(const QString&)` as the only entry accessor - https://raw.githubusercontent.com/qt/qtbase/6.8/src/corelib/io/qzipreader_p.h
9. Fedora licensing page for UnRAR (use restrictions; GPL-incompatible, non-free) - https://fedoraproject.org/wiki/Licensing/Unrar
10. SPDX LZMA SDK licence (public domain, stated compatible with GPL/LGPL code) and the 7-Zip SDK page version/date, which disagrees between the main site (26.01, 2026-04-27) and the `d.7-zip.org` mirror (26.03, 2026-09-03) - unconfirmed which is current - https://spdx.org/licenses/LZMA-SDK-9.22.html and https://d.7-zip.org/sdk.html
11. Qt development thread "Either remove or make QZip{Reader,Writer} official", May 2026 - found via search; the archive host was not reachable from this session, so the thread's contents are **unconfirmed** beyond the search summary - https://lists.qt-project.org/pipermail/development/2026-May/047180.html
12. libarchive supported formats wiki - reads ZIP, 7-Zip, tar (with GNU extensions), and RAR plus RAR v5 (read-only) - https://github.com/libarchive/libarchive/wiki/LibarchiveFormats
13. libarchive RAR, RAR5 and 7-Zip reader source headers - independent BSD-2-Clause implementations, no RARLAB code - https://raw.githubusercontent.com/libarchive/libarchive/master/libarchive/archive_read_support_format_rar5.c , `...rar.c` , `...7zip.c`
14. Ubuntu package advisories referencing a heap out-of-bounds read in libarchive RAR handling (CVE-2026-4424) and a RAR5 decoder flaw (CVE-2026-4111); packaging notes rather than upstream advisories, so treat the identifiers as **unconfirmed** - https://ubuntuupdates.org/package/core/noble/universe/updates/libarchive
15. (Review round 1) libarchive v3.8.10 `CMakeLists.txt` and `libarchive/CMakeLists.txt` - `ENABLE_*` switches cover external libraries only; format sources are compiled unconditionally; bundled BLAKE2 is compiled when libb2 is not used - https://github.com/libarchive/libarchive/blob/v3.8.10/CMakeLists.txt
16. (Review round 1) Launchpad, libarchive in Ubuntu noble - binaries `libarchive13t64`, `libarchive-dev`, `libarchive-tools`, version 3.7.2-2ubuntu0.9 - https://launchpad.net/ubuntu/noble/+source/libarchive
