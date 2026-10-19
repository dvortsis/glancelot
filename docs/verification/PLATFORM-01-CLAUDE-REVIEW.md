# PLATFORM-01 independent Claude review

Disposition: **APPROVED** (scoped; no blocking findings; physical gates remain open)

Candidate: `c35ec602f18a1e2bd1e42390fee3bd26e0c9efdd` (branch `claude/review-platform-01`,
merge-base with main `e63a4ab3d9848505c9b54d5a0576a0f177030f23`). HEAD verified, tree clean at start.
Reviewer lane: Claude Code (not the implementing Codex lane). Review worktree
`.worktrees/claude-review-platform-01`, build `build-claude-review-platform-01`. No production
source, shared header, root configuration or other worktree was modified.

Approval covers software packaging/verification tooling (`tools/platform`), the evidence
documents, and the two test prerequisites. It does NOT close any physical hardware gate and
does not assert Windows reproducibility. Integrity hashes are not signing/authenticity.

## Findings

No blocking or medium findings. All low/informational:

1. **Low - stale status text.** `docs/verification/PLATFORM-01.md:3` ("implementing, not independently
   approved") and `:77-78` ("final default Windows wrapper check ... is pending") contradict `:163-168`
   (final wrapper passed) and the handoff. Scenario: a reader of the top of the file believes the
   Windows wrapper gate is unrun. Fix at integration by updating status; `tools/platform/README.md:3`
   likewise says "is implementing".
2. **Low - toolchain pin recorded, not enforced.** `tools/platform/linux_package.sh:6,41` defaults to the
   mutable tag `glancelot-qt:codex-perf-measure` and records its inspected image ID in
   BUILD-ENVIRONMENT.json, but never compares that ID with the pinned
   `sha256:979a46b2...`. Scenario: a re-tagged image builds a package whose recorded ID differs and
   nothing fails. The runtime stage (`Dockerfile.package:24,26`) uses unpinned `ubuntu:24.04` plus
   `apt-get`, so the runtime smoke baseline is not itself reproducible. Both are acknowledged-scope
   limits; consider an expected-ID argument later.
3. **Low - ELF audit de-duplicates by content.** `dependency_audit.py:21-22` runs `ldd` once per unique
   SHA256, at the first path. Identical bytes at another path with a different `$ORIGIN`/RUNPATH
   context would not be separately resolved. Qualification (`qualification.py:51-56`) correctly
   requires every unique ELF digest to be covered; no current package is affected.
4. **Informational - isolated Python.** `qualification.py`/`package_smoke.py` import sibling
   `artifacts`, so they cannot run under `python -I`; `artifacts.py` itself can. Documented usage
   (plain `python`) works; I ran verify/extract of untrusted archives with `-I`.
5. **Informational - smoke harness moves its own window.** `package_smoke.py:113-137,165-174` restores
   and positions only the test process's window in the work area, and fails if it cannot. The frame
   oracle (`frames > 0`) and RGB oracle are unchanged. This is a controlled visible-window test, not
   display-latency evidence.

Audited with no defect found: real build identity from CMakeCache/CMakeCXXCompiler/Ninja flags and
Qt config (`collect_build.py`), with the smoke test comparing the running binary's version with the
manifest; Git vs `declared` provenance origins and the `-UseExistingBuild` requiring `-SourceIdentity`
(`windows_package.ps1:14-15`); fresh-prefix `cmake --install` with executable hash match
(`stage_windows.py:30-39`) and a single SBOM inventory (0 nested `qt-sbom/qt-sbom` entries, 137 files,
42 dirs); notices/Geist OFL included; Linux QML closure via resolved imports and ldd; bounded
manifest/ZIP central-directory/TAR header allocation; ZIP codec/encryption rejected before any
entry is opened (`artifacts.py:294-295`); portable inventory comparison as sets; extraction into a
new root with re-verification; qualification binding to manifest, executable hashes, source, target,
source media hashes, every unique ELF digest and both repeat archives. `physical_gates_closed` is
hard-coded False and every physical row is `unverified`. The two test-file diffs
(`platform_integration_tests.cpp`, the `grid_ui_tests.cpp` virtualized-row prerequisite) are
test-only; the launcher capture replaces QSKIPs with real asserted launches, and environment
variables are restored. No UI/QML source changed, so UI design skills were not applicable.

## Independent commands and results

All run by this reviewer in this worktree (author logs not counted).

| Gate | Result |
|---|---|
| `python tools/platform/test_artifacts.py -v` (Python 3.14.8) | 7/7 pass |
| `python -O tools/platform/test_artifacts.py -v` | 7/7 pass |
| `tools/platform/windows_package.ps1 -Output .cache/review-windows -Epoch 1792383552 -BuildDir build-claude-review-platform-01` (default path, runs `scripts/build.ps1 -Test -Package`; no `-UseExistingBuild`) | exit 0; CTest 100% passed 15/15, 234.31 s; fresh-prefix install, seal, verify; source `c35ec60`, dirty=false, origin=git; 137 files / 42 dirs; ZIP SHA256 `c7d5020a...eff8` |
| `artifacts.py -I verify-archive` / `extract` on that ZIP (new empty dir) | PASS / PASS |
| `package_smoke.py` on the extracted package (disposable generated PNG/JPEG) | PASS: 1024x768 PNG and 64x48 JPEG decoded; screenshot 1180x780 with red/green/blue 182,780 px each; frames 14, first frame 511 ms, graphics API 4; package-only Qt modules; missing-decoder diagnosed; media/package hashes unchanged. The native window did present frames in this interactive session (no limitation) |
| `qualification.py --case W-NATIVE` on that package | PASS; ELF closure and reproducibility rows unverified (not supplied); physical rows unverified |
| `linux_package.ps1 -Output .cache/review-linux-a -Epoch 1792383552 -Attempt claude-review-a` (WSL Ubuntu + Docker, pinned image ID confirmed `sha256:979a46b2...7aed7497`) | exit 0; full CTest in image passed; clean Ubuntu 24.04 non-root `--network=none` runtime extracted, audited 71 unique ELF files (all Qt/ICU in-package), smoked and qualified; 1,309 files / 48 dirs |
| Linux smoke (container, software GL) | PNG and JPEG decode, RGB 196,608 px each, API 3, frames 16, first frame 949 ms, missing decoder diagnosed |
| Re-ran `qualification.py --case C-CPU` on my extracted Linux tree | PASS; `package.elf-closure=passed`; reproducibility unverified; physical unverified |
| `compare` author's e4532d2 archive vs my c35ec60 Linux archive | `executables_equal=true`; only `LINUX-PACKAGE.md` and provenance metadata differ (documentation change plus my build's declared `dirty=true`, caused by my own untracked log files present at launch). Archive bytes differ as expected. This is a third independent compile with identical executables, but is not an equality claim for the archive |
| `verify-archive` on author's repeat archives a and b; `compare a b` | both PASS; both SHA256 `e9f1886a...2635e`; all equality fields true |
| Binding probes | Offering the author's repeat report (different source commit) with my package: rejected "repeat build conditions differ". Appending a byte to an extracted package file: rejected "package inventory differs" |
| `compare` author's final Windows ZIP (7c1e5a2) vs mine | executables and archive differ (only `bin/glancelot.exe`, `bin/glancelot-decoder.exe` plus metadata), consistent with the recorded Windows non-reproducibility |

Author's committed JSON agrees with its prose: Windows pair `archive_bytes_equal=false`,
`executables_equal=false`, differing bytes only at COFF/debug timestamp offsets (4 bytes per
exe); Linux pair identical at e4532d2 only. I preserved both scopes. Between e4532d2 and this
candidate the only Linux-relevant tool change is a portable sort key and ZIP codec guard; I
confirmed the Linux executables are still identical (row above).

## Missing coverage / limitations

- Windows bit-for-bit reproducibility: FAILED in the historical pair and not established; I did not
  perform a second independent Windows compile of this candidate.
- Linux reproducibility rests on the author's e4532d2 pair plus my executable match; I did not
  run a second Linux compile of c35ec60 to compare full archives, nor a second GUI launch.
- Author's second Linux archive reused the cached runtime smoke (documented), not a second launch.
- Physical/hardware gates unverified: DPI 100-200%, 60/144 Hz refresh, ICC/HDR, DECODE-01 R1-R4,
  native Linux X11/Wayland, separately provisioned clean Windows. Software-GL container smoke and
  an in-session Windows window prove none of these.
- Not run: large-MP decode, HDD/SMB, adversarial gzip-bomb TAR corpus beyond the author's tests,
  other hosts/compilers. Root Docker browser image build was not part of this packet and not run.
- My native Windows suite ran with a marker in my own worktree `.cache/native-suite-running`
  (removed afterward); the lead's marker in `../codex-perf-integration/.cache` had already been
  removed when I began.
