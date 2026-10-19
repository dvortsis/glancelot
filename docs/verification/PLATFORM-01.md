# PLATFORM-01 implementation evidence

Status: implementing, not approved or integrated. Frozen packet on main 444f896;
branch codex/platform-01, worktree .worktrees/codex-platform-01,
build directory build-codex-platform-01. Started from main 7347b9d after PERF reached
review (required combined Windows 15/15 203.16s and Docker 15/15 128.24s). Those are
PERF candidate checks, not PLATFORM package build evidence. No hardware gate closes.

## Initial artifact-integrity layer

The Python standard-library tools under tools/platform seal only generated staging
roots, inventory every shipped file/directory, generate deterministic ZIP/tar.gz,
verify actual archive entries without extracting them and compare two measured
archives. provenance.json excludes itself; an external sidecar hashes the completed
archive and manifest. Source identity distinguishes Git-derived and explicitly
orchestrator-declared revisions; no credentials, host/user identity, absolute source
paths or environment dumps are stored. Integrity hashes are not digital signatures
or proof that a declared revision compiled a binary.

Archive metadata normalizes order, timestamps, uid/gid and modes. Explicit epoch is
retained; ZIP timestamps use their inherent two-second precision. File/total sizes,
entry counts, manifest reads and ZIP central-directory/TAR metadata allocation are
bounded. Links must be materialized in staging; archive link/extension entries are
rejected. Invalid metadata, altered/missing/extra files, traversal, duplicates,
case-collisions, modes, timestamps and digest mismatches fail verification.

Compare reports actual archive bytes, executable/payload identities, metadata and
added/removed/changed files separately. A real changed test executable is detected
even when its version metadata is unchanged. No compiler byte-reproducibility claim
is made from these generated fixture tests.

## Commands and current results

```
Windows Python 3.14:
python tools/platform/test_artifacts.py -v
Ran 6 tests ... OK
python -O tools/platform/test_artifacts.py -v
Ran 6 tests in 0.438s ... OK

Linux Ubuntu 24.04 Python 3.12, read-only source mount, --network none:
docker run --rm --network none --entrypoint python3 [read-only source mount] \
  glancelot-browser:codex-perf-measure /workspace/tools/platform/test_artifacts.py -v
Ran 6 tests ... OK
```

Tests create disposable generated files and actual archives. They verify repeated
ZIP/tar byte equality, seven ZIP corruption/entry attacks, TAR extension/link/size
rejection, manifest constraints, source-tree mutation/omission/extra-directory and
Linux symlink rejection, plus exact binary-difference reporting. The duplicate ZIP
entry warning is intentional fixture construction; the verifier rejects that archive.
Python -O does not bypass production checks. No user media or external service is used.

## Still required

- Build-identity collection and Windows/Linux packaging wrappers using actual
  compiler/CMake/Qt/source identities and pinned Qt SBOM/notices.
- Full PLATFORM source Windows/Linux suites before packaging. The Linux launcher
  test prerequisite is in the PERF review candidate; it is not yet integrated into main.
- Actual Linux GUI package with viewer/decoder/Qt dependencies and offline launch/
  repair docs, separate from the authenticated browser container.
- Windows scripts/build.ps1 -Test -Package evidence; packaged launch without
  development Qt on PATH; image/decoder evidence and unchanged generated-media hashes.
- Two real build/package attempts; executable and archive-byte comparisons with
  differences honestly reported. Generated archive tests do not qualify compiler output.
- Independent dependency/artifact checks and clean-runtime package smoke.
- Qualification schema/scripts and Windows/Linux backend/DPI/refresh/decode matrix.
  Missing physical hardware produces explicit unverified rows. No physical color,
  HDR/ICC, high-refresh, native Linux display or HDD/SMB result has been measured here.
- Claude-lane independent review, serial integration and integrated full checks.
