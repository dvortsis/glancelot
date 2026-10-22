# PLATFORM-FIXTURE-SECURITY-01 independent source review (Claude PLATFORM lane)

Candidate: `fce50960c10dbfedcfe874bec518985fbe55e9b9` (branch `codex/platform-fixture-security`,
reviewed in worktree `claude/review-platform-fixture-security`, tree clean at that commit).
Baseline compared: `7cfcca1` (frozen contract) / `a599dcf` (queue assignment).

## Disposition

**Source acceptable for first bounded execution of this new helper and its test only**, subject to the
runtime, Defender-status, Windows/Docker and integrated gates in the plan. No changes are requested before
that first run. The security hold stays. This review does not certify the machine clean and does not show the
earlier `Behavior:Win32/DefenseEvasion.A!ml` detection was a false positive. It covers only what the new
source does.

Suggested first-run discipline (not source changes): run only `platform_integration_tests` once, and inspect
Defender status before and after. If Defender flags anything, stop, do not exclude, and escalate.

## Commands I personally executed (read-only)

`git rev-parse HEAD`, `git status --short`, `git diff --stat 7cfcca1 HEAD`, `git diff --stat 7cfcca1 HEAD -- src include CMakeLists.txt cmake Dockerfile scripts`,
`git ls-tree -r HEAD --name-only | Select-String 'platform_launcher_stub|native_platform_fixture'`,
`git log --oneline 7cfcca1..HEAD`, `git grep` for `ResumeThread|CREATE_SUSPENDED|CreateProcess|Thread32|CopyFile|QFile::copy|qputenv|platform_launcher_stub|native_platform_fixture`,
plus file reads of the documents and sources named in the task. I did not build, run, test or start Docker, and
I did not touch the old code, Defender or the root `.cache`.

## Verified facts

- `src/` and `include/` have an empty diff against `7cfcca1`. The product adapter and public header are unchanged.
  The root `CMakeLists.txt` is also unchanged, and it already includes `cmake/PlatformTestFixtures.cmake`.
  The Linux `#else` branch of `platform_integration_tests.cpp` and its `main()` are textually unchanged.
- `tests/platform_launcher_stub.cpp` and `tests/native_platform_fixture.hpp` are deleted. `git ls-tree` shows
  neither, and no live source references them. Only documents and review history mention them.
- Repo-wide grep finds no `ResumeThread`, `CREATE_SUSPENDED`, `CreateProcess`, `Thread32`, `CopyFile`,
  `QFile::copy` or PATH `qputenv` in the replacement. The test asserts `PATH` is unchanged in `cleanupTestCase`.
  The helper has no process creation and no event loop. It contains no self-copy, no Windows shell-name
  renaming and no suspended startup.
- **Seam scope.** `platform_integration_test_adapter.cpp` defines the `QProcess`, `QStandardPaths` and
  `SHOpenFolderAndSelectItems` macros only after including the real Qt/Windows headers and
  `platform_fixture_capture.hpp`. It then `#include`s the product `.cpp` and `#undef`s all three. The adapter is
  added only to `platform_integration_tests` (Windows branch). On Windows that target does not link
  `glancelot_platform`, so there is no duplicate symbol. `glancelot_platform`, `glancelot`, the decoder and the
  other tests compile the product file with no macros. Production cannot receive the seams.
- **Helper (`platform_argv_capture.cpp`)**, validated in this order:
  - argc is 2..66, so at most 64 payload arguments.
  - Nonce must be a non-null UUID that round-trips to canonical lowercase without braces. This rejects
    `../x`, braces and uppercase.
  - The root comes only from `GLANCELOT_TEST_ARGV_ROOT`. Its canonical path must have a basename starting with
    `glancelot-argv-`, and its canonical parent must equal the canonical temp directory. Canonicalisation
    resolves junctions and symlinks, so a link to elsewhere fails the parent/prefix test. The 8.3 short-name
    form is also normalised.
  - Payload is at most 20000 UTF-16 units. JSON is at most 256 KiB.
  - Output is `capture-<nonce>.json` opened `WriteOnly|NewOnly`, so an existing file is refused (exit 93) and
    never truncated. Distinct exit codes 91/92/93/94 cover the failure cases.
  - It reads no file and writes nothing else. It never executes anything and never touches media.
- **Parent pipeline (`captureArguments`)**:
  - Argument transport uses an ordinary owned `QProcess`. It runs `waitForStarted` and `waitForFinished`
    (2 s each), then `kill` and `waitForFinished(1 s)` on timeout. No `startDetached` is used for the helper.
  - The parent reads only after exit code 0.
  - It checks exact argument list equality, token, module (case-insensitive canonical compare with
    `helperProgram()`), CWD, and `console == false`. A missing `console` field defaults to true and fails closed.
  - It removes the generated file afterwards. On failure paths the file stays in the `QTemporaryDir` and is
    cleaned up by its destructor.
- **Coverage vs the plan.** All of these are present:
  - invalid and missing paths never launch;
  - directory reveal through the typed `QProcess` seam to the helper;
  - file reveal with real `ILCreateFromPathW`, PIDL and `SHGetNameFromIDList` verification, with real
    new-STA and existing-STA balance checks (`S_OK`, then `S_FALSE` nested, then `S_OK` after);
  - native-failure fallback to the `explorer.exe /select,` argv;
  - terminal directory, file-parent and root dispatch;
  - the four terminal fallback/failure combinations, each asserting the PowerShell literal-path escaping;
  - argv edge cases: no payload, empty, quoted, trailing backslash, Unicode, metacharacters, 64 arguments,
    20000 units;
  - helper rejection of missing, bad nonce, bad root, oversize, 65 arguments, and existing output (the original
    content is preserved).
- The test sandbox is `QTemporaryDir(tempPath/glancelot-argv-XXXXXX)`. The CWD is a generated directory with
  `' ; $() ☂ 🧪` and spaces, and the only file is a generated `photo ... .png`. No user media is used.
- `cmake/PlatformTestFixtures.cmake` fails closed when the replacement sources are missing. It builds the helper
  with `WIN32`, links only `Qt6::Core`, and sets its output directory to `native argv ☂ 🧪`. It never copies the
  helper. `platform_integration_tests` depends on it.

## Findings (prioritized; none blocks first execution)

1. **Evidence gap (medium).** `windows-compile.txt` has 13 lines and the step counters are inconsistent
   (`[1/7]`, `[2/5]`, `[3/4]`). It shows only the helper's MOC step and the test's compile and link. It does not
   show the helper's compile or link, nor the helper `.exe` in the Unicode directory. The handoff claims a
   9/9 build, but this file does not substantiate it. The runtime `QVERIFY(QFileInfo::exists(helperProgram()))`
   fails closed before any launch if the helper is missing, so this is not a safety risk. The lead should
   capture the helper's presence and hash before execution.
2. **Fidelity and portability risk (medium, fail-closed).** The helper path reaches the test as a
   compile definition (`GLANCELOT_TEST_ARGV_CAPTURE="$<TARGET_FILE:...>"`) containing non-BMP/non-ANSI
   characters (☂ 🧪). Ninja and cl command-line or response-file encoding can corrupt that on a non-UTF-8
   active code page. The same applies to the linker's output directory. The result would be a runtime
   failure of `initTestCase` (exists/contains checks), not unsafe behaviour. If it fails, fix by passing the
   directory through a UTF-8-safe route, for example `QString::fromUtf8` of an escaped value or a generated
   header. Do not weaken the Unicode requirement.
3. **Timing/flakiness (low).** Each successful dispatch waits at most 2 s + 2 s. There are about 25 helper
   launches and the CTest timeout is 30 s. On a cold Defender scan, or if the ordinary helper exe is
   scanned on first launch, a timeout is conceivable. A timeout produces `capture.error` and a failing test,
   never a hang. `QProcess` is killed explicitly. A bounded retry or a longer `TIMEOUT` is a reasonable
   tweak later, not a precondition.
4. **Console expectation (low).** `console == false` assumes a GUI-subsystem helper does not inherit a console
   when started via `QProcess` from a console parent. Windows normally behaves that way, but the first run
   will confirm. A mismatch fails closed with "Native argv, module, CWD or console mismatch".
5. **COM precondition (low).** `initTestCase` asserts that the QtTest main thread starts with no COM apartment
   (`S_OK` on `CoInitializeEx` STA). If Qt or the runner pre-initialised COM, the test fails closed
   (`RPC_E_CHANGED_MODE` or `S_FALSE`) rather than passing wrongly.
6. **Helper partial-write residue (info).** If the write or flush fails (exit 94) the partial JSON stays in
   the generated root until `QTemporaryDir` removal. This is acceptable for test use.
7. **Root validation breadth (info).** Any existing `glancelot-argv-*` directory directly under the temp
   directory is accepted as a root. Worst case is one new exclusive generated JSON file in it. No overwrite and
   no traversal is possible, because the nonce is canonical and the write is `NewOnly`.
8. **Untested adapter branch (info).** The `ILCreateFromPathW == nullptr` branch (and thus the matching
   `CoUninitialize` balance) is not exercised, since all test paths exist. This is outside the plan.

## Native versus controlled evidence (for the later handoff)

- **Native/real:**
  - Qt `QProcess` quoting and the helper's Qt/Windows command-line parsing, with a real process, real module,
    real CWD and a real exit code.
  - Real `ILCreateFromPathW`, PIDL composition and `SHGetNameFromIDList` on generated files.
  - Real COM apartment init and uninit balance.
  - Real filesystem checks in the adapter and the helper.
  - Qt `findExecutable` lookup of the helper by its friendly basename in an explicit directory (this tests Qt,
    not the adapter's `wt.exe` lookup).
- **Controlled/mocked:**
  - the adapter's `wt.exe` lookup result;
  - launcher success and failure sequencing (`launchResults`);
  - `SHOpenFolderAndSelectItems` (captured, not executed);
  - `explorer.exe`, `wt.exe` and `powershell.exe` names, which are inert recorded data.
- **Not proved:**
  - installed Explorer, Windows Terminal or PowerShell launching;
  - detached-process or UI behaviour;
  - the real Windows executable search order.

  This matches the handoff's own disclosure.

## Evidence gaps and open gates

- Neither the helper nor the test has run, so all runtime assertions are unverified.
- Defender observation of the first run and the post-run status inspection are pending.
- Windows full-suite, Docker, integrated-commit and Linux-branch regression checks are pending.
- Hold release is pending.
- Native Windows hardware/platform behaviour of Explorer, Terminal and PowerShell is unverified.
