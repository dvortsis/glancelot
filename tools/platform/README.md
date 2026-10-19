# Offline package tools

PLATFORM-01 is implementing; this is its artifact-integrity layer. Build/package
wrappers, real Windows/Linux package evidence, repeat-build experiments and hardware
qualification scripts remain to be completed. See docs/PLATFORM-01-PLAN.md.

artifacts.py uses Python's standard library and never extracts an archive. It seals
a newly generated staging directory, writes deterministic ZIP or tar.gz artifacts,
independently verifies their actual contents and compares two measured artifacts.
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
