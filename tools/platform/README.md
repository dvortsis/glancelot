# Offline package tools

PLATFORM-01 is implementing. See docs/PLATFORM-01-PLAN.md and
docs/verification/PLATFORM-01.md for scope and measured results.

artifacts.py uses Python's standard library. It seals
a newly generated staging directory, writes deterministic ZIP or tar.gz artifacts,
independently verifies their actual contents and compares two measured artifacts.
Its extract command first verifies the archive and writes only validated entries
into a new directory, with bounded allocation and normalized modes/times.
No command deletes an existing output or modifies a shipped payload.

The staging directory must contain .glancelot-package-staging with exact bytes
`1\n`. All staged symlinks must be materialized by the packaging wrapper. The
internal provenance.json inventories every payload SHA256, size, executable bit
and directory, excluding itself. An external archive-name.json hashes the completed
archive and internal manifest, avoiding circular/self hashes. These are integrity
checks, not digital signatures or proof that a declared source revision built a binary.

Source identity has commit, dirty and origin fields. --repo reads Git; alternatively
--source-identity accepts an orchestrator-provided JSON object explicitly marked
origin=declared for runtimes without Git. Build identity requires actual application,
Qt/compiler/CMake versions, configuration, C++ standard and path-free flag tokens.
Only pinned Qt 6.8.3 / C++20 and Windows/Linux x86_64 targets are accepted.

Pass an explicit --epoch (SOURCE_DATE_EPOCH). Archive entry ordering, modes, user/group
and times are normalized. ZIP uses its two-second timestamp precision; tar and
provenance retain the actual supplied epoch. Repackaging identical staged payloads
can prove archive reproducibility; two separate compiled builds are still required
before claiming compiler output is reproducible.

```text
python tools/platform/artifacts.py seal NEW_STAGE --repo . --build-info BUILD_INFO.json --target windows-x86_64 --epoch EPOCH --executable bin/glancelot.exe --executable bin/glancelot-decoder.exe
python tools/platform/artifacts.py verify-tree NEW_STAGE
python tools/platform/artifacts.py archive NEW_STAGE NEW_ARCHIVE.zip --format zip
python tools/platform/artifacts.py verify-archive NEW_ARCHIVE.zip
python tools/platform/artifacts.py compare FIRST.zip SECOND.zip
python tools/platform/test_artifacts.py -v
```

Compare prints archive-byte, executable, payload and metadata equality plus added/removed/changed
payload paths. Exit 0 means archive bytes match; exit 1 means they differ. Invalid
packages fail verification before comparison. It never describes matching version
strings alone as byte reproducibility.

The verifier rejects inventory omissions/extras, duplicate and case-colliding paths,
traversal, changed payloads/modes/timestamps, link/extension entries and digest
mismatches. Entry count, file/total sizes, manifest size and archive metadata allocation
are bounded. Tests use disposable generated files and actual archives, including
corruption and path attacks; they do not require user media or external services.

## Build, smoke and compare

Use the project-local Python on Windows. Outputs must be new. EPOCH is an explicit
Unix timestamp; use the same value and clean source revision for repeat experiments.

```powershell
$epoch = [long](git show -s --format=%ct HEAD)
./tools/platform/windows_package.ps1 -Output .cache/package-windows -Epoch $epoch
./tools/platform/linux_package.ps1 -Output .cache/package-linux -Epoch $epoch
python tools/platform/artifacts.py extract .cache/package-windows/Glancelot-windows-x86_64.zip .cache/extracted-windows
python tools/platform/package_smoke.py .cache/extracted-windows .cache/windows-smoke
python tools/platform/qualification.py --package .cache/extracted-windows --smoke .cache/windows-smoke/smoke.json --output .cache/windows-matrix.json --case W-NATIVE
python tools/platform/repeat_build.py --target windows --output .cache/repeat-windows --epoch $epoch
python tools/platform/repeat_build.py --target linux --output .cache/repeat-linux --epoch $epoch
```

Windows defaults to scripts/build.ps1 -Test -Package before sealing. Explicit
-UseExistingDeployment skips that invocation and validates deployment binaries
against the selected build; it does not create fresh test evidence. Run package
commands serially because the existing Windows deployment directory is shared
within this worktree. Linux requires WSL Ubuntu/Docker on Windows, or use
tools/platform/linux_package.sh --output NEW_OUTPUT --epoch EPOCH on Linux.
The Qt toolchain image is resolved to its exact Docker ID; its source build and
full CTest suite precede packaging. Independent runtime extraction, ELF audit,
software-GL X11 smoke and a qualification matrix must pass before export. No Qt SDK
or source checkout is present in that runtime. The normal browser image is separate.

Repeat builds require clean Git state, create fresh build directories and force
two separate Linux compilations. comparison.json records actual archive hashes,
executable/payload/metadata equality and changed files. Exit 1 reports differing
archive bytes; it is a measured result, not permission to claim reproducibility.
Read QUALIFICATION-MATRIX.md for physical gates and evidence reuse checks.
