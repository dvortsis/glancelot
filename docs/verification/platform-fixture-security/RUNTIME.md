# Replacement fixture runtime evidence

Source: fce50960c10dbfedcfe874bec518985fbe55e9b9. Actual independent Claude
source review: 752758c, retained as claude-source-review.md. It accepted only
the first bounded execution, subject to runtime and complete integration gates.

The first owned Windows run used Qt 6.8.3/MSVC2022 on Windows11:

```
$env:PATH='<project Qt bin>;' + $env:PATH
./build-codex-platform-fixture-security/platform_integration_tests.exe \
  -o docs/verification/platform-fixture-security/windows-first-run.txt,txt -o -,txt
```

Result: 26 passed, 0 failed, 0 skipped, 1190ms; process exit0. The per-case
log is windows-first-run.txt. Before execution, windows-first-run-preflight.txt
records the actual Unicode executable path, length23552, and SHA256
A63F8AA8AC77FFD65FDD34605409EEBDE8E1127B716404D10C007FF0B46F249F. This closes
the source review's missing helper presence/identity evidence; the old partial
compile log is retained without treating its inconsistent counters as a clean
full-build transcript.

The Unicode path/encoding, no-console and initial COM assumptions all passed.
The helper validates generated root/nonce/bounds/exclusive writes. Native owned
argv/CWD/module and real PIDL/COM checks passed; OS launcher names, lookup and
dispatch outcomes are controlled data. Installed Explorer, Terminal, PowerShell,
detached UI behavior and Windows executable search order remain unverified.

Read-only Defender checks before and after this run show only the earlier
ThreatID2147738096 detection, inactive, ActionSuccess=True, DidThreatExecute=False.
No new replacement detection appeared in those records. These observations do
not certify the machine or establish that the earlier detection was false.
No security settings were changed; the old helper was not restored or run.

The first required Docker build compiled the candidate and ran all29 targets;
28 passed, folder_grid failed at reviewCompactCellsSnapToWholeRows
(9 delegates, limit8, width300/height420). Linux platform_integration passed0.48s.
Full CTest elapsed145.47s. Exact failure output is linux-full-first-failure.txt.
The unchanged compiled image's isolated grid method then passed3/3, zero skips,
4083ms (linux-grid-isolated.txt). This diagnostic does not replace the failed
gate. The complete unchanged retry passed29/29 in158.32s and built the runtime
image (linux-full-retry-pass.txt). Compilation layers were cached; the entire
CTest layer reran. No UI/test limit was modified. The order/timing sensitivity
of the inherited grid test remains a recorded flake, not a resolved UI defect.

The required scripts/build.ps1 -BuildDir build-codex-platform-fixture-security
-Test passed29/29 in288.03s (windows-full-pass.txt); folder_grid passed176.07s,
platform_integration passed0.71s. Its exact Qt log passed26/26, zero skips,684ms
(windows-full-platform-cases.txt). Post-suite Defender records again show only
the earlier inactive/nonexecuted threat (windows-full-defender-after.txt).

Final independent evidence review, integrated-commit checks and security-hold
release remain pending. A separate
monitor_output_tests process was present during build/test monitoring; no desktop
or host isolation is claimed and no other lane process was terminated.
