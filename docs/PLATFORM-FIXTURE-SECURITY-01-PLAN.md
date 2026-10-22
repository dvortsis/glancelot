# Retired Windows fixture replacement contract

Lead-frozen corrective prerequisite after17e0cb1; FS remains in review. Branch
codex/platform-fixture-security, worktree .worktrees/codex-platform-fixture-security,
build build-codex-platform-fixture-security. Action act_mvgrfsa8_be3125ae52b0.
Independent reviewer: Claude PLATFORM lane under the user's standing approval.

Do not restore, allow-list, rebuild or execute platform_launcher_stub. Retire its
source and native_platform_fixture.hpp. No Defender changes, executable self-copy,
Windows shell-name impersonation, PATH replacement, suspended child startup,
thread enumeration/resume, shell execution or original-media access.

Preserve product platform_integration.cpp and its public header. Compile its
Windows test TU through typed private QProcess::startDetached and
QStandardPaths::findExecutable seams, alongside the existing typed
SHOpenFolderAndSelectItems capture. Include real Qt/Windows headers before
substitution, undefine seams after the TU, and never link them into production.
Tests keep real filesystem validation, PIDL construction/selection arguments and
COM apartment balancing. Capture requested launcher names/argv/CWD and controlled
lookup/failure outcomes without executing Explorer, Terminal or PowerShell.

Successful captured calls exercise a separate glancelot_test_argv_capture helper
using ordinary owned QProcess start/wait/timeout kill. Link only QtCore; build it
directly in a Unicode/space output directory, never copy it. It accepts a bounded
UUID nonce and at most64 payload arguments, writes at most256KiB JSON exclusively
to a newly created capture-<nonce>.json in a generated glancelot-argv-* temporary
root supplied only to that child, reports actual module/argv/CWD/console state,
and exits. Reject malformed roots/nonces, existing output and oversized input;
never overwrite media or run commands. Parent reads only after actual exit and
verifies exact payload/module/CWD. Native lookup tests use the helper's own
friendly basename and explicit search directory; no environment PATH mutation.

Require invalid/missing paths, Unicode/spaces/shell-metacharacter literals,
file/directory/root dispatch, native selection success/failure, new/existing STA
balance, Terminal success/failure and PowerShell fallback/false return. Add native
roundtrip cases for empty/quoted/trailing-backslash payloads, non-ASCII executable
and CWD, exclusive output refusal, bounded invalid controls, and actual process
exit. Label controlled lookup/dispatch vs native argv/COM/filesystem evidence.

Root CMake must fail closed until all replacement sources exist; include the
fixture only for Windows/testing, with file/stdout QtTest sinks. Keep Linux tests
and production adapter unchanged. Compile before review, but independently review
the replacement source before its first execution. Keep the old detection and
failed gate evidence. Release the security hold only after review, native tests,
Defender status inspection and required Windows/Docker/integrated checks. A lack
of new alerts is evidence for those runs, not a general antivirus certification.
