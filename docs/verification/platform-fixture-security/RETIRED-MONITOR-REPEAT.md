# Repeated detection from an older lane snapshot

After replacement qualification, read-only Defender inspection recorded two more
detections of the retired helper in
.worktrees/monitor-output/build-monitor-output/platform_launcher_stub.exe,
at09:01:55 and09:02:18 local on October20. Both record ActionSuccess=True and
last status change09:02:47. Current Get-MpThreat still reports IsActive=False and
DidThreatExecute=False. Exact records are retired-monitor-repeat-detections.txt.
These are protection records, not a forensic clean-machine determination.

That older worktree still contains tests/platform_launcher_stub.cpp and its
CMake builds the retired target. The older monitor suite ran concurrently with
the integrated replacement check and restarted again after that check finished.
Its snapshot is not covered by replacement qualification334065e. No detection
for glancelot_test_argv_capture appears in the inspected records. The earlier
integrated post-run snapshot predated these additional old-fixture detections.

The lead requested user permission to stop only the separate monitor-output
test run; at this record the reply is pending and no other lane process has been
terminated. Its owner must stop old-fixture runs and merge main before another
Windows gate. Do not restore, exclude, allow-list, rebuild or execute the retired
helper. The new guarded FS runner still validates qualified ancestry, identical
replacement source and absent retired sources; its current full32 gate is not
yet finished and no host-isolation claim is made.

The user subsequently authorized stopping the monitor-output suite. At09:15-09:16
the lead revalidated the active tests by their exact build path and stopped the
monitor thumbnail test PID53416, then its live CTest ancestor PID52232. The
runner used a working directory rather than a build-path argument, so process
ancestry from the actual test path was required to identify it. A final inventory
showed no remaining process under that build directory. No source file, viewer,
implementation agent, unrelated application or Defender setting was changed.
The earlier request is resolved; future full runs still require qualified main.
