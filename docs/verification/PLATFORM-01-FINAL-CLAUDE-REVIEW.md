# PLATFORM-01 final software close-out review (Claude lane)

Candidate `6b93a4dd379d4f9774e817cc3dc8d44d7a285d5a` (integrated source `1d51c17`
plus documentation-only commits). Reviewer: Claude Code lane, not the implementing
Codex lane. Worktree `.worktrees/claude-review-platform-closeout`, branch
`claude/review-platform-closeout`. Read-only audit of repository files and Git.

## Disposition: APPROVED (SOFTWARE packet closure only)

Scope: the PLATFORM-01 software deliverables 1-6 and the corrective Windows
native-launcher fixture. This does NOT close CANVAS-UI, COLOR-01 or DECODE-01 and
does not approve FS-WATCH or INPUT UI. The native-fixture approval (`8888d41`) was
not substituted for this audit; each deliverable was checked separately below.

**Recorded no new runtime execution this review.** I built nothing, ran no tests,
Docker, probes or Python, stopped no process, closed no window and changed no machine
setting. Everything below is a read of committed files and Git objects. The `.cache`
logs the author cites live outside this worktree and were not read; gate outputs
are therefore the author's recorded results, cross-checked against Git for what they
are bound to (see "Source binding").

## Prior reviews relied on (preserved, unchanged)

| Report | Candidate | Disposition |
|---|---|---|
| `PLATFORM-01-CLAUDE-REVIEW.md` (`cfba419`) | `c35ec60` | APPROVED. I confirmed `c35ec60` is an ancestor of this candidate and `cfba419` is not (review lives on its own branch). That review's own default Windows wrapper (15/15) and Linux package/runtime runs are its evidence. |
| `PLATFORM-01-INTEGRATION-CLAUDE-REVIEW.md` (`130da47`) | `73fa157` | CHANGES REQUIRED: `directory_model` 250 ms assertions failed (942/806 ms); cause not established; next evidence must isolate native work, then establish cause or validate a supported fix, then pass integrated gates. |
| Native fixture proposal and implementation review (`8888d41`, Docker wiring `7bb7b80`) | `b4fa53e`, `5b03339` | Fixture-only runtime APPROVAL (`platform_integration` 9/9, CTest 0.79 s); Docker one-line COPY ACCEPTED statically. Explicitly not a full-gate or full-platform approval. |

## Deliverable audit

1. **Linux GUI archive** - `tools/platform/` contains `linux_package.sh/.ps1`,
   `Dockerfile.package`, `stage_linux.py`, `dependency_audit.py`, `linux_smoke.sh`,
   `LINUX-PACKAGE.md`. Evidence in `docs/verification/platform-01/`: `linux-build-info.json`
   (GNU 13.3.0, CMake 3.28.3, Qt 6.8.3, -O3 C++20), `linux-dependencies.json` (ELF audit),
   `linux-merged-smoke.json`, `linux-merged-matrix.json` (source `9698ff0`, origin
   `declared`, ELF closure and smoke checks `passed`). Documented: 1,309 files, 71 unique
   ELFs audited in a network-disabled non-root clean Ubuntu runtime; SBOM, notices and
   Geist OFL shipped. Browser image separate and unchanged (Dockerfile diff is the one
   COPY line only).
2. **Windows portable evidence** - `windows_package.ps1` (default runs
   `scripts/build.ps1 -Test -Package`; `-UseExistingBuild` requires a declared source
   identity), `stage_windows.py`, `package_smoke.py`. Committed `windows-merged-smoke.json`
   and `windows-merged-matrix.json` match the narrated results; development Qt removed
   from PATH, generated media, missing-decoder case and unchanged hashes recorded.
3. **Provenance and integrity** - `artifacts.py`, `collect_build.py`; schema 1, external
   sidecar, no self-hash, bounded parsing, ZIP codec/encryption rejection, new-output-only
   directories. Reviewed in `cfba419` with the 7/7 (plain and `-O`) `test_artifacts.py`
   runs by that reviewer. Documented as integrity, not authenticity or signing.
4. **Repeat comparison** - `repeat_build.py`. Committed results are consistent and honest:
   `linux-comparison.json` status `identical` (both archives
   `e9f1886a...2635e`, epoch 1792383552, source `e4532d2`, two independent builds);
   `windows-comparison.json` status `different` (ZIPs `c0dba2b3...` vs `91fe8070...`);
   `windows-pe-differences.json` shows 4 differing bytes per executable, all COFF/debug
   timestamp fields, with no bytes rewritten. The Windows result is correctly NOT
   claimed as reproducible. The Linux claim is scoped to those inputs/host/toolchain.
5. **Qualification matrix** - `qualification.py`, `QUALIFICATION-MATRIX.md`, schema 1.
   `linux-merged-matrix.json` shows `physical_gates_closed: false`; CANVAS-UI physical DPI
   and refresh, COLOR-01 ICC/HDR, DECODE-01 R1-R4, native Linux and clean Windows are all
   `unverified` with reasons; `reproducibility.independent-builds` is `unverified` in
   that single-build matrix rather than borrowing the earlier pass. Observed rows are
   limited to package/Qt-isolation/decode/RGB/decoder/no-write/ELF closure. Uncontrolled
   environment fields (cache, power, GPU identity) are recorded as such.
6. **Docs and gates** - `docs/verification/PLATFORM-01.md` contains commands, counts
   and limitations. Its stale-status items from the first review are corrected, and its
   top "Status" line still says "integration checks pending" (see Finding L1).

## Corrective gate and source binding

- `git diff --name-only 73fa157 1d51c17 -- app src include ui tools platform` is empty
  (exit 0). Product source, public headers and packaging tools are byte-unchanged.
- Non-doc deltas `73fa157..1d51c17` are: `CMakeLists.txt` (one line: the test target's
  link line replaced by `include(cmake/PlatformTestFixtures.cmake)`), `cmake/PlatformTestFixtures.cmake`
  (Windows: GUI stub target, test-only definitions and `add_dependencies`; Linux `else()`
  retains the original link line), `cmake/FSWatchTests.cmake` (not included by this
  root; inert), `Dockerfile` (`COPY cmake cmake`), and four test files. No install target,
  product dependency or product link change.
- `git diff --name-only 5b03339 1d51c17` outside `docs/` is empty. Every path the Docker
  build COPYs (`CMakeLists.txt cmake app assets include src ui tests`) is therefore
  identical between the actual 18/18 Linux execution (110.41 s on `5b03339`) and `1d51c17`.
  This makes the later CACHED identical image (`0ba808df...2692`) a valid statement of
  "same inputs, same tested layer", not independent evidence of a new execution, and I
  rely on it only in that sense. `1d51c17..6b93a4d` touches only three docs files.
- `tools/platform` differs from reviewed `c35ec60` only by one README line, and is
  identical to merged source `9698ff0`. The package evidence (Windows ZIP `cd241e81...`,
  Linux archive `01ffda17...`) stays bound to its recorded sources; I do not claim packages
  rebuilt at `1d51c17` or byte-identical Windows compiler output. The post-`9698ff0`
  changes are test-only and not shipped.
- Windows gate (author-recorded): `scripts/build.ps1 -BuildDir build-codex-platform-native-fixture -Test`
  18/18, 176.11 s (`folder_grid` 95.20, `directory_model` 21.60, `platform_integration` 1.14),
  strict 250 ms oracle and timeouts unchanged, quiet native slot for 30 s before and no
  competing suite seen. Windows fixture also independently run by me in the earlier
  review (9/9, 0.79 s). Full-suite Windows execution at `1d51c17` is author-run, not
  reviewer-run; I could not read the `.cache` logs from this worktree.

## Answer to the decision

Yes for SOFTWARE packet closure, with the residual risk stated explicitly.

The `130da47` condition was: isolate native work, establish the cause or validate a
supported fix, then pass the required integrated gates. The remaining requirement is
met in part and deliberately scoped:

- Met: the packet's own gates (Windows 18/18, Docker/Linux 18/18, no assertion or
  timeout relaxed, no product change) pass on the integrated source, the failure was
  not hidden, and the genuine test-hygiene defect (unit tests spawning real persistent
  Shell UI with weak request assertions) is fixed with an independent runtime review.
- Not established, and not claimed: the cause of the earlier 942/806 ms stalls and the
  flake frequency. One passing quiet-slot run does not show the fixture removed the
  stall. Per the original diagnosis, no threshold, priority or power change is
  authorized. This is a standing risk to the 250 ms `directory_model` oracle in future
  gates, not missing PLATFORM-01 software evidence. I judge it an acceptable open item
  for this packet because no deliverable, acceptance criterion or shipped artifact
  depends on it, and the contract's gates are "Windows and Linux full suites before
  packaging" - satisfied - rather than a flake-rate statistic.

No concrete missing software evidence blocks closure.

## Findings (none blocking)

- **L1 (low):** `docs/verification/PLATFORM-01.md` line 3-4 still reads "serial integration
  checks pending" and line 266 "final independent software evidence audit is pending". The root
  should update that status text when it records the disposition. No evidence is affected.
- **L2 (low):** the Windows 18/18 and Docker logs are in the author's `.cache`, outside any
  committed record; only the narrated numbers are in Git. Consider committing sanitized
  gate excerpts, as was done for smoke/matrix JSON. Carried from the author's own note.
- **L3 (low, carried from `cfba419`):** Linux toolchain image ID is recorded, not enforced; the
  runtime stage uses unpinned `ubuntu:24.04` plus apt, so the runtime smoke baseline is not
  itself reproducible.
- **L4 (informational):** the native fixture needs 8.3 short names on the TEMP volume
  (fails closed otherwise), cannot prove visible Explorer/terminal presentation, uses an
  invalid-executable stand-in for "PowerShell absent", and may leave inert TEMP copies on
  parent abort.
- **L5 (informational):** `cmake/FSWatchTests.cmake` is copied into the Docker context by
  `COPY cmake cmake` but not included; FS-WATCH is not approved by this review.

## Remaining unverified (unchanged, not closed here)

Physical DPI (100-200 %), cross-monitor transitions, 60/144/240 Hz refresh, ICC/HDR/color
accuracy, DECODE-01 R1-R4 (large-MP, 8 GiB/HDD, discrete GPU, SMB), native Linux
X11/Wayland, independently provisioned clean Windows, Windows bit-for-bit compiler
reproducibility (measured `different`), future flake rate, and stall cause.
CANVAS-UI and DECODE-01 stay open. Screenshots and software flags were not treated as
physical evidence.

## Hygiene

Only this file was added. No implementation, header, CMake, queue, merge or push edit
was made; the root owns queue state after this disposition.
