# PLATFORM-01 implementation evidence

Status: independently approved by Claude cfba419 for candidate c35ec60; serial
integration checks pending. Frozen contract
444f896; branch codex/platform-01, worktree .worktrees/codex-platform-01,
build build-codex-platform-01. F080 / PERF-BUILD basics are included. Physical
CANVAS-UI, COLOR-01 and DECODE-01 gates remain open.

## Delivered behavior

tools/platform contains local Windows/Ubuntu x86_64 Qt 6.8.3 packaging, actual
configured build collection, bounded artifact verification/extraction, repeat-build
comparison, independent ELF auditing and generated-image smoke. Linux GUI packaging
is separate from the authenticated browser image; root browser defaults are unchanged.

provenance.json inventories every payload SHA256, size, canonical archive executable
bit and directory, excluding itself. Source records commit, dirty state and Git or
explicitly declared origin. Build metadata comes from actual CMakeCache, compiler/Qt
configuration and Ninja flags. An external sidecar hashes archive and manifest without
circular self-hashing. These establish integrity, not signatures or binary authenticity.

New outputs only; originals are never modified. Archive ordering, uid/gid, modes and
times are normalized under explicit SOURCE_DATE_EPOCH (ZIP uses two-second precision).
Links must be materialized. File/total/entry/manifest/central-directory/TAR allocation
is bounded. Extraction verifies first, writes only validated entries into a new root,
normalizes modes/times and verifies the tree and unchanged archive.

Two reproduced verifier defects were corrected: an unsupported ZIP codec could open
a native decoder despite a small advertised size; Windows path ordering rejected an
unchanged Linux inventory. Stored ZIP is the supported ZIP encoding; other codecs
and encryption fail before opening any entry decoder. New manifests use exact relative
component ordering; existing inventories verify independently of array order. Path,
size, hash, duplicate/collision, link, extra/missing and archive-mode/time checks remain.

Windows defaults to scripts/build.ps1 -Test -Package, then installs into a new package
prefix. Reusing dist had inherited a nested SBOM copy; fresh installation excludes
stale plugins/files and copies notices/SBOMs directly from sources. Deployed binaries
must match the configured build. Explicit -UseExistingBuild (old alias
-UseExistingDeployment) requires declared recorded source identity and does not
create fresh compile/test evidence or silently label an old binary as current HEAD.

## Windows measurements

Two independent builds at clean 765a84efc0bae712006e767bf59a9f63a4196399,
epoch 1792381582, used fresh assigned build directories a and b:

```text
python tools/platform/repeat_build.py --target windows --output .cache/platform-windows-repeat --epoch 1792381582
scripts/build.ps1 -Test -Package, attempt a: 15/15, 250.73 s
scripts/build.ps1 -Test -Package, attempt b: 15/15, 287.54 s
metadata_equal=true; executables_equal=false; archive_bytes_equal=false
```

Only bin/glancelot.exe and bin/glancelot-decoder.exe differed. Each has four differing
bytes, all within COFF/debug timestamp fields. windows-pe-differences.json records
exact hashes/offsets; no timestamp bytes were rewritten. This pair does NOT establish
bit-for-bit Windows reproducibility.

Packaging e4532d2 freshly deployed those same independently tested builds with source
765a84e explicitly declared. C++/QML/CMake inputs were unchanged between those commits.
Both corrected packages have 137 files, 42 directories, 141,578,536 payload bytes and
one SBOM inventory. There are no added/removed/differing nonbinary payloads. ZIP hashes:

```text
a c0dba2b38aa0679e28cf5dad09b79dbe279180e686137f086604ce7f01e7eba9
b 91fe80709d3ca1e2318b8f69aacbe651035c116b813e534fe11ab3c0080e5e8a
```

MSVC 19.44.35229.0, CMake 4.4.4, Qt 6.8.3, C++20 Release, app 0.1.0-alpha.1;
exact flags are in windows-build-info.json. Corrected package A passed PNG/JPEG,
actual package-only Qt/qwindows module inspection with development Qt removed from
PATH, missing-decoder diagnosis and unchanged source/package hashes. Its screenshot
has 196,608 red, green and blue pixels each (1180x780); first PNG/JPEG frames 703/530 ms,
API 4 and DPR 1. Tiny-fixture observations are not controlled performance or physical
color evidence. The corrected ZIP was independently extracted, then its matrix was
rechecked against both actual archives.

The final default Windows wrapper at 7c1e5a2 and independent reviewer default wrapper
at c35ec60 passed, as recorded below and in the unchanged Claude review report.
Earlier reproducibility results above remain scoped to their exact builds.

## Linux measurements

Two independent compilations/packages at clean e4532d2315ebb6af3df84186c83134c7d7431794,
epoch 1792383552, used pinned toolchain image
sha256:979a46b2bb4fdaea301fe14f6a532deac1fb4ae5721933d247fa513a1aed7497.

```text
python tools/platform/repeat_build.py --target linux --output .cache/platform-linux-repeat-final --epoch 1792383552
attempt a: full CTest 15/15, 136.82 s
attempt b: full CTest 15/15, 106.35 s
executable/payload/metadata/archive equality: all true; added/removed/changed: []
both archive SHA256:
e9f1886ad53964b37b35f5df4c32bcfe6d2707ffc80143491048a8f26be2635e
```

Distinct BUILD_ATTEMPT arguments forced separate CMake/compile steps; these are not
two archives of one build. Repeatability is established for these measured inputs,
not another host/compiler/epoch/dependency tree. GNU 13.3.0, CMake 3.28.3, Qt 6.8.3,
app 0.1.0-alpha.1; flags -O3 -DNDEBUG -std=c++20 -fPIC. Both packages have 1,309 files,
48 directories and 133,354,392 payload bytes. Viewer SHA256
81a2559bd8410044fbba3f82fb25879f38cc3e753dd739d91f582f2f6ff732fb;
decoder 4178851da19653c2f0b9a60c88b4ab5943be1637ea9dcd0d0cccde128e2d1cc9.

A separate nonroot Ubuntu 24.04 runtime has no Qt SDK, compiler or source checkout.
With network disabled it verifies/extracts the archive and audits all 71 unique ELF
payloads with ldd; Qt/ICU must resolve exclusively inside the package. System SONAMEs
remain explicit OS dependencies. Authenticated localhost-only X11/software GL smoke
publishes no ports. PNG/JPEG decode, actual RGB screenshot content, package-local
Qt/xcb modules, missing decoder and unchanged source/package hashes passed. B's
byte-identical artifact reused the successful runtime audit/smoke cache; it is not
a second independent GUI launch. Its compilation/tests/package comparison were independent.

PNG/JPEG first frames 1,459/477 ms, API 3, DPR 1, 196,608 pixels of each RGB color.
These are observations with uncontrolled CPU/cache/power state. The matrix was
independently regenerated on Windows against the exported Linux tree, ELF report
and both archives after fixing inventory ordering. Its executable/archive
reproducibility rows pass; physical rows remain unverified.

## Targeted validation and failed attempts

```text
Windows Python 3.14 test_artifacts.py -v: 7/7 pass
Windows Python 3.14 python -O test_artifacts.py -v: 7/7 pass
Linux Python 3.12, pinned image/read-only source/tmpfs/no network: 7/7 pass
```

Actual disposable archives cover repeat ZIP/tar bytes, extraction, content changes,
missing/extra entries, traversal, duplicates, links, modes/times, manifest constraints
and TAR extension/size bounds. Eight ZIP attacks include a valid-inventory LZMA entry
that reproduced acceptance before the decoder guard. Reordered valid inventories
reproduced tree-verification failure before the portability fix. Both regressions
now pass alongside unchanged corruption checks; -O cannot disable production guards.

Generated probes reject altered smoke identity, missing-decoder success, absent RGB,
omitted ELF audit content and Windows repeat archives offered as Linux evidence.
Existing-build packaging without declared identity fails before creating output.
Invalid/failed repeat experiments return 2; measured differing archives return 1,
identical archives return 0.

Earlier Linux attempts failed closed: WSL path conversion (fixed), unused QML import
closure requiring absent QtSvg (fixed by resolved-import deployment), and missing
system libgssapi_krb5.so.2 (documented/provisioned). Failed attempts exported no accepted
artifact. Resource-interrupted attempts ended with critical disk/WSL shutdown, not
a completed second package; they were not reproducibility evidence. The final clean
two-build experiment above supersedes them.

## Evidence and remaining gates

Sanitized generated JSON copies are in docs/verification/platform-01. Their relative
artifact paths retain original report bases; copies are records, not relocated CLI
inputs. Commands use original .cache locations: platform-windows-clean-{a,b},
platform-windows-clean-comparison.json, platform-windows-clean-a-smoke and
platform-linux-repeat-final/{a,b,comparison.json}. Some redundant staging/negative/
object copies were removed for disk headroom; archives, captures, hashes and source/
build metadata remain. Historical pre-fix Windows ZIPs are stored losslessly as
.zip.gz and byte-verified against original sidecars; decompress before rechecking.

Qualification binds exact manifest/executable/source/target identities, source media,
PNG/JPEG/frame/module evidence, all unique ELF content and both actual repeat archives.
It rejects another package/target instead of borrowing a pass. Reference-case labels
are not attestations. Offline operator instructions are in
 tools/platform/QUALIFICATION-MATRIX.md and tools/platform/LINUX-PACKAGE.md.

Final default Windows wrapper at clean source 7c1e5a2 passed 15/15 in 191.16s
and produced ZIP df56405ce6df8dcfb4e9acbaca5e83fe481c08e1394b2af91f6c1788991deeff.
The independently extracted package passed package-only Qt module checks, generated
PNG/JPEG decode, strict positive frame-swap and RGB screenshot checks, missing-decoder
diagnosis and unchanged media/package hashes. Evidence: .cache/platform-windows-
final-visible-smoke/smoke.json and .cache/platform-windows-final-matrix.json.

An initial run decoded and grabbed correct RGB pixels but reported zero frame swaps.
The user confirmed an interactive desktop; process and Explorer session IDs matched.
Native inspection found a visible, non-minimized, non-cloaked window near the lower
desktop edge. Moving the SAME binary into the primary work area and bringing it
forward yielded four swaps and first-image-frame 2014ms. The private smoke harness
now restores and positions only its own process window in that work area before
measurement. With this setup the full strict smoke passed; no frame oracle was
relaxed and no product rendering source changed. This establishes a controlled
visible-window test, not physical display latency or a definitive driver diagnosis.

Pending: serial integration and checks on the integrated commit. Physical DPI/refresh/ICC/HDR,
native Linux desktop/Wayland, separately provisioned clean Windows, large-MP decode,
HDD/SMB and R1-R4 results remain unverified. No original media, credentials, telemetry,
publishing or machine-wide configuration is involved.
## Reviewed merge candidate and package checks, 2026-10-19

Actual independent Claude review cfba419 APPROVED c35ec60, with no blocking or
medium findings. Report is preserved unchanged. Stale status wording was corrected.
Main b22b1b0 merged as c0a7795; the only conflict was the equivalent explanatory
comment in the already-reviewed virtualized-row prerequisite. Main's version and
all real assertions were retained. No product QML was edited by this packet.
Merged package source is clean Git 9698ff01f2b384e0aa4a062f96bc660e3f7af1af.

Required root Docker build passed all 18 tests, 115.55s, image
770ee4ef33c35111e2f4cfbe5192bef4cf3aa8740f2f747d9574c118419d2d94.
Linux GUI packaging independently compiled and passed 18/18, 115.51s, then its
non-root network-disabled clean runtime passed archive extraction, 71-ELF dependency
audit, PNG/JPEG strict frame/RGB/module smoke and evidence-bound qualification.
Linux archive SHA256 01ffda17740e11c040a21c69e4eb693b5262b8796b8fb5180698a634d0f921be.
This is one current-source build; historical repeat claims remain scoped above.

First merged Windows default wrapper failed directory_model (290.22s total), with
no detailed Qt output captured by CTest. A direct diagnostic run with an explicit
Qt log passed all 20 cases (16.534s). The full default wrapper was rerun with other
heavy checks paused, without source changes or relaxed assertions: 18/18 passed,
177.54s, then fresh-prefix install/seal/verify passed. The initial failure cause is
not established; contention is a possibility, not a verified diagnosis.
Windows ZIP SHA256 cd241e81c43c783e4c6488c16c57e2fe0151bb74d0e8bbb35eeef2484b364b17.
Independent extraction of that ZIP passed strict PNG/JPEG frame and RGB smoke,
package-only Qt modules, missing-decoder diagnosis, unchanged media/package hashes
and bound qualification. API4, PNG first frame601ms/13swaps; JPEG435ms/16swaps.
The software-GL Linux PNG was608ms/15swaps; JPEG315ms/15swaps. These are uncontrolled
tiny-fixture observations, not performance or physical presentation claims.

Local logs: .cache/platform-merged-{windows,windows-retry,linux,docker}.log and
platform-merged-model-diagnostic.txt. Generated artifacts/evidence are in
.cache/platform-merged-windows-retry, platform-merged-windows-smoke,
platform-merged-windows-matrix.json and platform-merged-linux/qualification.
Sanitized current smoke/matrix records are committed beside historical records.
Independent approval and pre-merge gates are complete; integrated-commit checks
remain before the lead closes software packaging. Physical gates stay unverified.
