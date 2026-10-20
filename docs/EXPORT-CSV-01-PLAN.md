# Task packet: EXPORT-CSV-01 (F070 selection CSV and path-list export engine)

- **Milestone:** M4 (engine only)
- **Requirements:** F070, SYS-06 (never writes original media), SYS-02 (bounded work)
- **Owner lane:** Gemini (`agent/*`). The lead reassigned it from the Codex Wave 5 row on 2026-10-20 (docs/AGENT-LANES-PLAN.md).
- **Branch / worktree / build:** `agent/export-csv` / `.worktrees/export-csv` / `build-export-csv`
- **Reviewer:** Codex or Claude lane, whichever reviewer slot frees first. The reviewer is never from the Gemini lane.

## 1. Scope

The task implements the frozen contract in `include/glancelot/selection_export.hpp`, and nothing else.

- `src/core/selection_export.cpp`, in `glancelot_core`. It uses only standard C++20, with no Qt.
- `tests/selection_export_tests.cpp`. Follow the style of `tests/file_operations_tests.cpp`: `check()` counters, plus generated and disposable fixtures under the system temp directory.
- No QML, no `ViewerController` command and no settings. The Qt service and export dialog are a later Claude-lane packet. **Do not edit `ui/Main.qml`** or any file outside the two above.
- The JSON selection manifest is F030's format and is out of scope.

## 2. Frozen interfaces (do not change without the lead)

- `include/glancelot/selection_export.hpp`: `ExportItem`, `ExportRequest`, `ExportResult`, `encodeCsvField` and `exportSelection`. Reuse `FileIdentity`, `TargetKind` and `snapshotTarget()` from `file_operations.hpp`.
- `cmake/ExportTests.cmake`: the source and the `selection_export` ctest target. The root `CMakeLists.txt` includes it once `src/core/selection_export.cpp` exists. Do not edit either CMake file.

## 3. Behaviour

- **CSV format.** Follows RFC 4180: a comma separator, CRLF line endings, and a header row with the column names (`path,name,folder,kind,bytes,modified`) followed by the metadata keys.
  - A field is quoted when it contains a comma, a quote, CR or LF.
  - Embedded quotes are doubled.
  - An optional UTF-8 BOM is written at the start.
- **Formula neutralization.** When `neutralizeFormulas` is set, a `'` goes in front of any field that starts with `=`, `+`, `-`, `@`, TAB or CR. This includes file names and metadata values.
- **Unicode.** Paths are converted to UTF-8 with `path::u8string`. Invalid UTF-8 in metadata becomes U+FFFD, and neither crashes nor silently drops data.
- **Relative paths.** With `RelativeToRoot`, paths use `/` separators relative to `root`. Items outside `root` are skipped as `OutsideRoot` and never fall back to absolute. Compare lexically on normal paths; Windows compares drive letters case-insensitively.
- **PathList format.** One path per line with LF line endings. A path containing CR or LF is skipped as `Unrepresentable`.
- **Revalidation.** Each item is re-snapshotted. A missing item is skipped as `Missing`, and an item whose identity or size/modified changed is skipped as `Changed`. Items with the same identity are written once (`Duplicate`).
- **Destination safety.**
  - `DestinationIsItem`: the destination is the same file as any item (identity or equivalent path). This applies even when `overwrite` is set.
  - `DestinationExists`: the destination already exists and `overwrite` is not set.
  - The file is written to a unique temp file in the destination directory, flushed, then renamed. Replacement uses an atomic rename where the platform has one.
  - Any failure or cancellation removes the temp file and leaves an existing destination unchanged.
- **Bounds.** More than `maxItems` items returns `TooMany` and writes nothing. Cancellation is checked between items. `progress` is called no more than once per item.
- **Generation.** `generation` is always echoed back, including on early-return statuses.

## 4. Verification and acceptance

`tests/selection_export_tests.cpp` must cover each of the following:

1. Quoting: commas, quotes, CR/LF, empty fields, a leading or trailing space kept.
2. Formula injection: all six prefixes, with neutralization on and off. A negative number such as `-5` is still prefixed when on (documented behaviour).
3. Unicode: CJK, emoji and RTL file names round-trip byte-exact. Invalid-UTF-8 metadata becomes U+FFFD.
4. Relative-root handling: inside, nested and outside the root, the root itself, and a sibling with a common prefix (`/a/b` vs `/a/bc`).
5. Revalidation: a file deleted after the snapshot (Missing), a file replaced with the same name (Changed), and a duplicate identity.
6. Destination: refuse an existing file without overwrite, replace with overwrite, refuse the destination when it is one of the items even with overwrite, and refuse a destination directory that doesn't exist (Failed).
7. Cancellation before and mid-export (use a stop source triggered from `progress`). There must be no destination file and no temp file left over, and an earlier destination file must be unchanged.
8. `maxItems` exceeded writes nothing, and the generation is echoed on every status.
9. PathList output: LF endings, no BOM, CR/LF in a path gives `Unrepresentable`.

The handoff must include:
- Actual Windows output for `ctest -R selection_export`.
- A full `scripts/build.ps1 -Test` run and `docker build -t glancelot-browser:main .`. Run these only while holding `gate_lock` in docs/WORK-QUEUE.yaml.
