# Integrated replacement qualification

Integrated commit334065e61f8362956918578672556ad9e922474a, independently reviewed
source fce5096 and evidence c289dfd. Actual Claude reports752758c and e7afce5 are
retained here. Production src/include/app and the Linux platform test body are
unchanged from7cfcca1. Later0abd078 changes only queue/security documentation.

Windows ran the required scripts/build.ps1 -BuildDir
build-codex-platform-fixture-integrated -Test on that exact merged commit in
.worktrees/codex-platform-fixture-integrated, branch
codex/verify-platform-fixture-security. Full compile and29/29 tests passed,
364.45s. Platform CTest passed0.82s; its exact Qt log passed26/26, zero skips,
748ms. Folder-grid passed245.38s under its unchanged300s timeout. See
integrated-windows-pass.txt and integrated-windows-platform-cases.txt.

The initial main-checkout Windows configure was explicitly stopped because Qt's
import scanner traversed sibling worktrees and encountered unrelated malformed
QML. Only the verified owned CMake/scanner processes were stopped. That attempt
is aborted, not a passing gate; integrated-root-configure-aborted.txt preserves
it. No other lane's files/processes were changed. Use isolated verification
worktrees for integrated native Qt checks, avoiding recursive sibling scans.

Docker command docker build -t glancelot-browser:main . ran on main at334065e,
with .worktrees excluded from context. Compilation and the complete CTest layer
ran freshly,29/29 passed305.76s; platform passed0.48s and folder-grid passed209.45s.
Runtime image export succeeded; see integrated-linux-pass.txt. The original
candidate grid failure and unchanged passing retry remain retained. There is no
claim that its order/timing sensitivity was fixed.

Read-only post-run Defender snapshots (integrated-defender-after.txt) still show
only ThreatID2147738096 against the old FS-tree launcher helper, IsActive=False,
DidThreatExecute=False and ActionSuccess=True. No new replacement detection
appears in these snapshots. They do not prove a scan occurred, certify the
machine, or establish a false positive. No protection changes, restore or
exclusion was used. The earlier failed FS31/32 run remains failed.

Only the replacement is qualified. Keep the old fixture retired and do not run
old lane snapshots. The local qualification record remains present so legacy
runners fail closed; new runners must check qualified commit ancestry, identical
fixture source and absent retired sources. Native argv/module/CWD, filesystem,
PIDL and COM assertions are real; requested OS launchers/lookup/dispatch outcomes
are controlled seams. Installed Explorer/Terminal/PowerShell launches, detached
UI behavior, executable search precedence and physical hardware remain unverified.

Another lane's monitor-output suite overlapped and still had old fixture source.
No desktop/host isolation is claimed. The lead requested permission to interrupt
that separate run; it was not terminated without a reply. That older snapshot is
not covered by this replacement qualification and must merge main before reuse.
