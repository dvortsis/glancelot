# PLATFORM-01 Windows native test-fixture prerequisite proposal

Lead planning proposal, October19,2026; not frozen implementation permission or
approval of a future patch. PLATFORM remains integration with failed Windows
responsiveness checks. Actual independent Claude diagnosis is evaluating this
proposal/control evidence under the user's standing PLATFORM review approval.

## Evidence and limits

Repeated native gates invoke real Explorer/terminal UI in
 tests/platform_integration_tests.cpp. Linux already uses capturing test launchers;
Windows still calls the real launcher and SHOpenFolderAndSelectItems, with no
corresponding window cleanup. A full current-source gate failed directory_model
and the platform30s timeout, while the two filesystem targets passed. Process
inventory found58Explorer processes using about8.4GiB; Shell inventory57folder
windows. Their test ownership and causal role in latency are NOT established.
No unowned window has been closed. A private local location inventory is excluded
from external review payloads; closing those windows requires the user's approval.

Stock Qt6.8.3 controls (no product model) showed10-16ms gaps across eight trials
per idle/pool-watcher/std-post/pool-post mode. After12,000 generated-file setup,
four fixture-pool trials measured582/157/479/387ms. A250ms pre-drain did not
reliably remove gaps; off-thread generation with GUI pumping still produced a326ms
gap in one of four trials. Thus no arbitrary wait, timer, priority, threshold or
model-thread rewrite is justified. Preserve the250ms product test assertions.

## Proposed Windows launcher isolation

Keep the production adapter, public headers and glancelot_platform library unchanged.
This is a lead-owned qualification/test prerequisite, not a takeover of Gemini's
PLATFORM-OPS feature implementation. Freeze these test/CMake details only after
independent assessment of the proposed seam:

1. Add a private Windows-only test adapter translation unit that includes the
   Win32/Shell declarations first, substitutes only the external
   SHOpenFolderAndSelectItems boundary with a typed capture function, then compiles
   the exact production platform_integration.cpp. The Windows test links this
   adapter instead of also linking the production library; there must be no duplicate
   symbol or alternate reimplementation of argument-building logic. Linux continues
   linking the real production library and using its existing capture helper.
2. The typed Shell capture checks and records the actual absolute folder PIDL,
   selected child PIDLs and flags using read-only path conversion, with bounded
   output. Exercise successful native selection and explicit native failure followed
   by the actual QProcess Explorer fallback. Do not invoke real shell UI. COM/path
   construction behavior that remains real must be stated; a stub is not proof of
   visible Explorer selection.
3. Build a separate Windows GUI-subsystem launcher stub through lead CMake. It has
   no QCoreApplication/event loop, console allocation or UI. It captures the actual
   UTF16 Windows command-line arguments, program basename, working directory and
   pid into the parent's unique generated JSON capture path, then exits. Bound
   input/output and validate the capture result. It never interprets or executes
   the captured arguments. Do not ship/install it.
4. Copy that stub only into a unique temporary fixture directory as explorer.exe,
   wt.exe and powershell.exe; never put fake system-named executables in the normal
   product/build directory. Use only process-local CWD/PATH/capture variables and
   the actual Qt DLL location needed by the copied stub. Restore the original
   environment and CWD after tests. Windows search order and DLL lookup must be
   independently verified so a missing fixture cannot accidentally open real UI.
5. Strengthen existing positive assertions with actual captured directory/file
   targets, Unicode/spaces/literal characters, cwd and argument-array parity. Cover
   terminal file-to-parent behavior, wt and PowerShell fallback, missing-launcher
   failure, native Shell success/fallback and no unexpected capture on invalid
   targets. Preserve every existing meaningful assertion; no skip/disabled case.
6. Confirm each captured helper process exits normally before deleting the capture
   or executable fixture. Cleanup must target only proven owned helpers; never kill
   unrelated shells or processes. Generated preferences/media only. Hard test
   termination may leave a bounded temporary file fixture, not persistent desktop
   windows or user-data writes.

## Required validation and ownership

The lead freezes exact file/target/seam ownership under the serial lock before
implementation. Candidate source must show no production adapter/public API change.
Independently review the code and native helper/argv capture, including loader
search order, PIDL conversion, fallback/error/cleanup cases and Unicode quoting.
Run required full Windows and Docker gates on the resulting candidate; actual
Windows desktop Explorer/terminal presentation remains a separate manual/platform
qualification. An argument capture cannot claim it measured that UI.

This prerequisite can eliminate future test-created desktop side effects and make
launcher assertions deterministic. It does not by itself fix or waive the unresolved
responsiveness failure. Keep PLATFORM open until all required integrated checks and
independent review are satisfied. FS-WATCH will consume only a reviewed prerequisite
under a lead amendment; no parallel second Codex implementation packet is started.
