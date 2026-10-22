# Replacement native fixture candidate

Implements the lead-frozen PLATFORM-FIXTURE-SECURITY-01 plan; not an integration
approval or a claim that the original detection was false. Security hold stays.

Product platform_integration.cpp/header and non-Windows tests remain unchanged.
Windows test TU captures only process dispatch/terminal lookup and the existing
typed shell-selection seam. The helper executes under its own friendly basename,
built directly in a Unicode/space directory. It has no process/shell creation,
self-copy, PATH changes, suspended startup, thread manipulation or event loop.
It writes bounded JSON only to an exclusive generated UUID file in a validated
temporary root. Requested Explorer/Terminal/PowerShell names remain inert data.

Adapter argument values traverse an ordinary owned QProcess to the real helper;
module, exact argv, inherited/explicit Unicode CWD, no console and actual exit
are checked. Lookup outcome and launcher failure are controlled seams. These
tests do not prove installed Explorer/Terminal/PowerShell launch, detached process
UI behavior or Windows executable search precedence. Real filesystem/PIDL/COM
construction and balancing remain exercised. Linux retains its existing native
launcher tests. No original photos, credentials or security settings are used.

Compile-only commands: .cache/build-only.ps1 bootstraps the installed MSVC2022
environment, configures CMake Release/BUILD_TESTING/Qt6.8.3, then builds only
platform_integration_tests (including its helper dependency). First compile
completed9/9 steps. Final table additions recompiled and linked successfully;
actual configure/build output is platform-fixture-security/windows-compile.txt.
No replacement executable/test has run yet: independent review is required first.
Read-only Defender baseline still reported old ThreatID2147738096 inactive and
not executed, with no detection record for the new helper at inspection time.

Review must check test-only substitution scope, exact argv/CWD pipeline, root and
nonce validation, exclusive writes, bounds, process cleanup and retained coverage.
Pay attention to empty/quoted/trailing-backslash payloads, maximum64 arguments /
20000 UTF16 units and rejected malformed/missing/oversized controls. Runtime,
Defender observation, Windows/Docker/integrated gates and hold release are pending.
