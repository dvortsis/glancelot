# PLATFORM-01 frozen contract

Frozen by the user-appointed Codex lead on October 19, 2026. This packet implements
the complete Wave 1 PLATFORM-01 + F080 + PERF-BUILD basics scope. It preserves
REQUIREMENTS.md and the remaining physical qualification gates.

Lane: Codex. Branch: codex/platform-01. Worktree: .worktrees/codex-platform-01.
Build: build-codex-platform-01. Independent reviewer: Claude Code lane.
No stable external plugin ABI or new public C++ header is needed.

Start after PERF-MEASURE-01 reaches review with required candidate build gates.
Do not overlap two active Codex implementation packets. PERF integration may remain
pending cross-lane approval while PLATFORM implementation proceeds after that gate.

## Required deliverables

1. A Linux Qt 6.8.3 GUI package from the existing Docker toolchain, containing the
   viewer and decoder together, needed Qt libraries/plugins/QML modules, notices,
   fonts license, SBOM and offline launch/repair documentation. The existing
   authenticated browser container remains a separate deployment artifact.
2. Windows portable-package evidence using `scripts/build.ps1 -Test -Package`.
   Smoke launch with development Qt removed from PATH, disposable media and isolated
   preferences; verify source hashes and child-decoder behavior.
3. A versioned provenance JSON manifest: application/Qt/compiler/CMake versions,
   source commit and dirty state, architecture, relevant build flags, executable
   and package SHA256, shipped dependency/SBOM inventory and SOURCE_DATE_EPOCH when
   supplied. Never describe provenance alone as bit-for-bit reproducibility.
4. Repeat-build/package comparison scripts. Normalize archive ordering/timestamps
   under explicit SOURCE_DATE_EPOCH. Compare actual bytes; report differences and
   scope honestly. Do not enable LTO/PGO or change codec/ISA policy in this packet.
5. Qualification scripts and a matrix for Windows/Linux backend, DPI/refresh and
   decode scenarios. Separate observed build/render/backend facts from unverified
   physical color/HDR/monitor/SMB/high-refresh/clean-machine gates. A flag or generated
   screenshot is not sufficient physical evidence. Keep CANVAS-UI / DECODE-01 open
   until their required real-hardware results are independently reviewed.
6. Exact commands, terminal results and platform limitations in
   `docs/verification/PLATFORM-01.md`; Claude-lane independent review and serial
   integration checks on the integrated commit.

## Permitted changes

- New packaging/provenance/qualification scripts under `tools/platform/`.
- `docs/PLATFORM-01-PLAN.md`, verification and offline package/repair docs.
- CMake install/build-identity changes if current Qt deployment is
  insufficient; keep changes in a small isolated task block. No new public C++ ABI
  is needed unless the lead explicitly requests an app-visible build identity.
- A separate Docker packaging target is permitted if needed. Preserve the current Docker
  browser runtime/defaults and its authentication, source mounts and test gate.
- No edits to ui/Main.qml, bindings, platform-integration service, original media,
  deployment credentials, automatic updates, cloud publishing or paid services.

## Acceptance evidence

- Windows and Linux full suites before packaging. Current unrelated baseline
  failures must be resolved by their owner or explicitly triaged by the integrator;
  no skipped/disabled assertions to manufacture a green package.
- Artifact contents and dynamic dependencies checked independently. A Linux
  package launch in a clean runtime container proves that container environment,
  not a native Wayland/X11 physical desktop or another distro.
- Two measured builds/packages for any reproducibility claim. Keep executable
  identity and archive identity separate; include exact differences when they differ.
- Missing optional hardware/profiling tools create explicit unverified matrix rows.
  Required build/package tools fail clearly. No machine-wide configuration changes
  or data-cache flushing are performed automatically.


## Frozen artifact contracts

- tools/platform/ contains offline build/package, provenance, artifact-verification,
  repeat-build comparison and qualification scripts; Windows PowerShell and Linux
  shell wrappers may share Python standard-library logic. No dependency downloads
  outside the existing pinned toolchain build steps.
- Package provenance schema 1 records application version, exact source commit/dirty
  state, target OS/architecture, compiler/CMake/Qt versions, configuration/flags,
  SOURCE_DATE_EPOCH when supplied, and relative-path SHA256 inventories. Never
  include host/user identity, absolute source paths, credentials or environment dumps.
- The internal package manifest hashes binaries and shipped files, excluding itself;
  an external sidecar hashes the completed archive and manifest. Do not create a
  circular archive/self hash. Verification rejects missing, extra, duplicate,
  traversing or symlink-escaped package entries and altered content.
- New output/staging directories only; never recursively delete an arbitrary existing
  destination or modify originals. Validate resolved paths before any cleanup.
- Distinguish repeatable archive bytes from repeatable compiler output. Record both
  attempts' actual hashes and differences; do not conceal compiler nondeterminism.
- Qualification schema 1 has explicit passed/failed/unverified status and evidence
  artifact references for each matrix row. Automated checks may establish observed
  backend/image dimensions/frame availability, not physical color/DPI/refresh truth.
- All default tools are local/offline. No automatic updates, telemetry, publishing,
  credentials, machine-wide config changes or filesystem-cache flushing.

## Lead-owned build prerequisites

The PLATFORM-OPS-01 Linux tests currently assume desktop launchers in the Qt build
container. The lead authorizes a separate test-only infrastructure repair: disposable
launcher processes must capture exact argv/cwd and tests must assert those requests,
invalid-input nonlaunch and missing-launcher failures. This must not change the
Gemini platform adapter or merely replace launch assertions with unconditional success.
Real desktop/terminal UX remains separately unverified. Keep this prerequisite in
its own branch/worktree and review it independently; do not claim it is a PERF fix.

Root CMake/Docker changes remain lead-owned: prepare an exact proposed diff in the
implementation handoff; the lead commits it under the serial lock before the lane
builds against it. Existing browser runtime, authenticated startup and full test gate
must remain. No product QML, input routing or cache/scheduler policy changes.

## Review and integration

Record actual Windows/Linux full-suite commands, package launch/hash evidence,
repeated-artifact comparisons and explicit hardware limitations in
 docs/verification/PLATFORM-01.md. Independent Claude review is required. The lead
integrates under the queue lock and reruns full checks on the integrated commit.
A package smoke in a clean container qualifies only that environment. Physical
CANVAS-UI/DECODE-01 gates stay open until their real-hardware evidence is measured.

## Frozen Windows qualification-fixture prerequisite, October19

Lead freezes the test-only prerequisite assessed by independent Claude64f0af0.
That review supports the design, not unimplemented code or a passing gate; the
Windows250ms oracle remains failed and unchanged. Implement within the current
FS-WATCH worktree as a shared qualification prerequisite, not a second active
Codex implementation. Product src/app/platform_integration.cpp and public headers
remain byte-unchanged; no Gemini adapter feature is taken over.

Permitted files: tests/platform_integration_tests.cpp,
tests/platform_integration_test_adapter.cpp, tests/platform_launcher_stub.cpp,
tests/native_platform_fixture.hpp, and evidence/docs. The lead owns prepared
cmake/PlatformTestFixtures.cmake and its root CMake inclusion. Enable it under
lock on the feature branch only after real source files exist. Windows must use
the exact production CPP in the private test TU, after Windows/Shell headers and
a typed substitution of only SHOpenFolderAndSelectItems. Do not also link the
production object in that test. Linux keeps its current real-library/test path.

The GUI-subsystem stub has no QCoreApplication/event loop/window and captures
actual UTF16 argv, cwd, its module path and pid into bounded per-invocation JSON.
Do not install it. Fake explorer/wt/PowerShell executables may exist only in a
unique temporary test application directory, never the normal product/build dir.
A copied test runner in that private directory addresses application-directory
search precedence; copied helpers find the actual project Qt DLLs through a
process-only environment. Verify canary self/module paths before positive calls.
Restore CWD/environment and bound child lifetime below the existing30s CTest
deadline, including parent-abort cleanup. Preserve the30s test timeout.

Capture Shell PIDLs by semantic path/equality, not raw bytes; validate folder/child
and flags, successful selection and native failure/Explorer fallback, balanced
known STA initialization, directory and file-to-parent targets, wt/PowerShell
fallback, Unicode/non-BMP/spaces/single quotes and missing-launcher failure. Check
actual captured argv, not reconstructed command strings. A missing helper must not
accidentally launch a real system program; validate native search results first.
Assert owned helpers exit before cleanup. Global new-process counts alone do not
prove test ownership and must not authorize stopping unrelated apps.

No source/performance assertion is skipped or relaxed. A captured request is not
proof of visible Explorer selection or terminal presentation. Independently review
and execute the implemented fixture, then rerun required full Windows/Docker gates
and integrated checks. This fixes future desktop side effects; it does not establish
the cause of the independent responsiveness noise floor.

### Windows fixture PATH representation clarification

Qt6.8.3 QStandardPaths default lookup reads PATH with qgetenv and
QString::fromLocal8Bit (qtbase src/corelib/io/qstandardpaths.cpp462-490).
The native wide lookup and explicit QString search find the non-BMP sandbox
while default lookup does not. Permit GetShortPathNameW of the same private
sandbox as the PATH entry only: validate identical directory identity and
ASCII representation, fail closed if unavailable. Keep the copied runner,
application directory, CWD, capture paths and media arguments in the original
non-BMP sandbox; no substitute ASCII sandbox or product adapter change.
Root resumes implementation of the existing four test files after its assigned
implementer hit a usage limit; actual Claude remains independent reviewer.
