# PLATFORM-01 contract proposal for the lead

This is a proposal, not a frozen contract or an active implementation. PERF-MEASURE-01
remains the Codex implementation task. The Wave 1 handoff requires the lead to freeze
`docs/PLATFORM-01-PLAN.md` before coding the next packet.

Source scope: `docs/AGENT-LANES-PLAN.md`, PLATFORM-01 + F080 + PERF-BUILD basics,
with qualification harnesses for the still-open CANVAS-UI / DECODE-01 hardware gates.
Do not change any Gemini-owned platform-operations, cache or scheduler API.

## Proposed deliverables

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

## Proposed permitted changes

- New packaging/provenance/qualification scripts under `tools/platform/`.
- `docs/PLATFORM-01-PLAN.md`, verification and offline package/repair docs.
- Lead-approved CMake install/build-identity changes if current Qt deployment is
  insufficient; keep changes in a small isolated task block. No new public C++ ABI
  is needed unless the lead explicitly requests an app-visible build identity.
- A separate Docker packaging target may be needed. Preserve the current Docker
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

Requested branch/worktree/build names from the approved handoff:
`codex/platform-01`, `.worktrees/codex-platform-01`, `build-codex-platform-01`.
The lead promotes this proposal only after the current packet reaches review.
