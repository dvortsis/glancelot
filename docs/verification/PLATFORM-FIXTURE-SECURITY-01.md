# Windows launcher fixture security hold

On October20,2026 at08:03 local, Defender detected
Behavior:Win32/DefenseEvasion.A!ml for the locally built
.worktrees/codex-fs-watch-01/build-codex-fs-watch-01/platform_launcher_stub.exe.
The initiating process was that tree's platform_integration_tests.exe. The
Windows gate failed with Win32 error225. The user supplied the matching alert.

Read-only Defender inspection recorded ThreatID2147738096, ActionSuccess=True
at08:03:50, followed by IsActive=False and DidThreatExecute=False. The flagged
file was absent. These are Defender's current records, not a forensic guarantee
that the machine or any other artifact is clean. No protection settings changed;
the file was not restored or allow-listed.

Source inspection found a test helper that captures arguments/CWD to generated
JSON. The surrounding harness copies its runner and renames helper copies as
explorer.exe, wt.exe and powershell.exe, substitutes PATH/CWD, and starts a
suspended child before assigning a job and resuming its thread. This is a
plausible behavioral trigger; it does not prove the detection is false.

The old harness is retired. Main's Windows test configuration now fails clearly
instead of building or running it. Do not suppress, exclude, restore or rename the
flagged binary to get tests passing. Do not start old-fixture Windows gates from
other lane snapshots. Existing gate output remains failed/unqualified.

A lead-frozen replacement must eliminate executable impersonation, self-copy and
manual suspended-thread startup. Preserve adapter argument/CWD validation through
typed test-only seams, plus a separately reviewed native Unicode/argument roundtrip
using a plainly named helper, without executing a shell or user commands. Keep
original-media isolation, bounded lifetimes and honest native-vs-mocked evidence.
Independent PLATFORM review and complete new gates are required before release.
The product adapter and viewer behavior are not changed by this hold.
