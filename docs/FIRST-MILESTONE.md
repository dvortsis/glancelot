# Glancelot first milestone: prove the core before building the surface

Planning baseline: October 17, 2026. This is an implementation backlog, not work already performed. See [the master plan](BUILD-PLAN.md) for product boundaries and [the requirement matrix](REQUIREMENTS.md) for IDs.

## Current status and next work

The bootstrap sequence and estimates below are the historical feasibility backlog. Windows now has the initial JPEG/PNG alpha, filmstrip, folder tree, window slots/resume, and folder grid. The full audit corrected persistence, scheduling and UI defects and independently verified the grid and name/modified/size/shuffle sorting before further M3 expansion. See [WORK-QUEUE.yaml](WORK-QUEUE.yaml) and [CLAUDE-AUDIT.md](verification/CLAUDE-AUDIT.md) for exact commits and evidence.

The approved M2.1 sequence was TREE-01 -> WINDOW-SLOTS-01 -> WINDOW-RESUME-01. Those Windows features preserve slot ownership across close order, retain unopened positions, honor explicit file opens, and use atomic coordinated settings. They do not automatically reopen all remembered windows.

Next bounded work is platform qualification and the remaining M3 foundations: revision-validated cache/lookahead, shared I/O admission, favorites, filters, metadata HUDs, richer input contexts, and qualified raster/RAW previews. The live queue owns priority and implementation state. M0/M2 requirements for Linux GUI, mixed-DPI/ICC/HDR, reference-hardware performance, fresh-machine installs, and accessibility are still open; implementation on one workstation does not satisfy them.

## M0 goal and budget

In 3-4 focused engineer-weeks, determine whether the proposed stack can deliver correct, responsive viewing on the actual Windows/Linux support targets. Produce small disposable experiments, written decisions, benchmark results, and UX wireframes. Do not spend M0 building the entire settings UI, adding every codec, or designing an extensibility framework.

Owner labels below are responsibilities; one maintainer may hold all of them. None implies an existing staffed team.

| Item | Responsibility | Depends on | Concrete deliverable and exit criterion |
| --- | --- | --- | --- |
| P0-01: support and benchmark baseline | Maintainer/platform | None | Record Windows/Linux versions, compositor, GPU/driver, monitor scale/refresh/HDR, storage and RAM. Choose reproducible JPEG/PNG/RAW/color/alpha/large-image fixtures with redistribution rights. |
| P0-02: product contracts | Maintainer/product | None | Confirm root boundaries, natural-sort comparator, temporary filter clearing, direction landing, preview labeling and file-write policy. Capture unresolved user preferences as assumptions. |
| P0-03: build/package probe | Platform | P0-01 | Pin one released Qt/compiler/dependency set; build minimal Qt Quick application on both OSes; run a bundled prototype on machines without developer tools. Record dependency inventory. |
| P0-04: GPU canvas probe | Graphics | P0-03 | Render a known buffer via QQuickRhiItem; exercise D3D12/Vulkan plus likely fallback. Test resize, mixed DPI, device loss and a 60 MP workload; measure offscreen composition cost. |
| P0-05: startup and worker probe | Core/performance | P0-03 | Shell-launch JPEG/RAW-preview path with worker process/shared memory; record timestamps for process/UI/read/decode/color/upload/present. Compare warm/cold caches and isolation overhead. |
| P0-06: color and pixel mapping probe | Graphics | P0-04 | Prove physical 1:1 on fractional scaling; display reference ICC patches; inspect compositor ownership and monitor transitions; report actual HDR capabilities without promising support. |
| P0-07: traversal oracle | Core | P0-02 | Independent reference sequence generator plus candidate lazy iterator on nested/empty/denied/symlink trees. Prove next/previous symmetry and filter-reset policy without UI. |
| P0-08: mutation and watching probe | Platform/core | P0-03 | External overwrite, truncate, atomic replacement, rename and deletion while viewing; no exclusive lock; show old good frame until valid replacement; report watcher timings. |
| P0-09: memory and scheduling probe | Performance | P0-04, P0-05 | Simulate rapid bidirectional navigation and four 60 MP panes under 8/16 GiB budgets. Demonstrate hard admission limits, cancellation and no obsolete image replacing current selection. |
| P0-10: accessibility/window probe | UX/platform | P0-03 | Frameless resize/move, palette keyboard path, focus/IME, screen-reader labels; identify Wayland restrictions and click-through recovery options. |
| P0-11: license/codec review | Maintainer/release | P0-03 | Proposed application license and dependency compatibility ledger. Verify metadata library source notices, RAW license choice and HEIF/video distribution questions; defer unqualified codecs. |
| P0-12: wireframes and workflow test | UX | P0-02 | Canvas/HUD/grid/compare/settings/error sketches; walk through five core tasks with representative users; record and resolve discoverability/recovery failures. |
| P0-13: architecture decision | Maintainer | P0-04 through P0-12 | Accept Qt/C++ or record measured reasons for an alternative. Freeze module contracts and support matrix; update estimates and performance gates without masking failed assumptions. |

Tasks without dependencies can be scheduled independently. The critical sequence is baseline -> build -> GPU/worker/color probes -> architecture decision. A stack alternative should be time-boxed against the same evidence, not become an indefinite comparison exercise.

## Suggested first ten working days

| Days | Work | Review artifact |
| --- | --- | --- |
| 1-2 | Support baseline, fixture rights, product contracts, initial toolchain build | Baseline manifest, first ADR drafts, reproducible build notes |
| 3-4 | Cross-platform canvas, JPEG opening and worker boundary | Installable experimental viewer; launch/decode/present trace |
| 5-6 | Physical 1:1, ICC baseline, large image and GPU memory | Pixel-pattern/color results; cache budget measurements |
| 7-8 | Traversal oracle, external mutation and cancellation | Passing domain fixtures; watcher/race traces; known failures |
| 9-10 | Frameless/focus/keyboard wireframes, capability and license review | Initial UX evidence; risk ledger; decision issues for weeks 3-4 |

Weeks 3-4 close cross-platform failures, collect repeated timing distributions, validate representative users' workflows, and make the stack decision. A single successful machine/demo does not complete M0.

## ADRs to write

- ADR-001: application stack and rendering integration; alternatives, measured startup/frame timing, compatibility costs.
- ADR-002: source identity, traversal root/order, sort snapshots and temporary filters.
- ADR-003: source/working/output color spaces, alpha, physical zoom and HDR capability boundary.
- ADR-004: worker isolation, parser limits, shared-memory IPC and cancellation.
- ADR-005: CPU/GPU cache budgets, storage adaptation and resource-pressure fallbacks.
- ADR-006: application/dependency licenses, packaging baseline and capability claims.
- ADR-007: command routing, UI contexts, config schema and window recovery.
- ADR-008 before M4: XMP ownership/interoperability, file mutation, conflict detection and recovery.

## Initial implementation sequence after M0

1. **Foundation:** create build/test/package skeleton, stable IDs and command types, config loader/migration, process IPC, error vocabulary, and licensed fixture manifest. Exit: both OS builds run and tests can be reproduced from a clean checkout.
2. **Open and display:** shell/drop/file dialog -> source identity -> JPEG/PNG worker -> immutable pixels -> color/orientation -> GPU canvas. Exit: first correct image presents without folder scanning or full metadata parsing.
3. **Navigate:** directory snapshots, natural file order, lazy folder traversal, boundary modes, filter reset, next/previous commands. Exit: oracle tests pass and rapid navigation never shows stale results.
4. **Inspect:** physical-pixel fit/zoom/pan, cursor anchor, loupe restore, view-only rotation and background presets. Exit: geometry/color fixtures pass at mixed DPI and after resizing.
5. **Control:** registry-driven palette, core shortcut/wheel remapping, safe fullscreen/frameless controls, persistence. Exit: keyboard-only open/view/navigate/remap/close works.
6. **Accelerate:** memory admission, neighboring preview/decode cache, bounded uploads, cross-folder prefetch and external-change reload. Exit: benchmark and pressure tests pass before adding more UI surface.
7. **Package alpha:** minimal usage/help and capability page, installable artifacts, known-limitations list. Exit: daily browsing works on both declared platforms and preserves original files.

Each item should become a small sequence of reviewable changes. Domain rules and integration checks accompany the behavior they verify. Do not delay Linux verification until the Windows implementation is complete.

## Proposed repository layout once implementation starts

```text
app/                     process startup, CLI, composition root
src/core/                commands, navigation, identity, viewport math
src/sources/             filesystem provider; future archive provider
src/scheduler/           priorities, cancellation, memory admission
src/worker/              decoder/metadata process, IPC validation
src/codecs/              decoder adapters and capability descriptors
src/render/              GPU canvas, color, resampling, diagnostics
src/metadata/            read models and later XMP merge policy
src/fileops/             later safe mutations and operation journal
src/platform/            Windows, Wayland, X11 integration
ui/                      QML views, controls, settings and accessibility
tests/                   domain, integration, renderer, package tests
tests/fixtures/          small licensed corpus and provenance manifest
benchmarks/              workloads, trace tooling, machine manifests
packaging/               Windows/Linux packaging definitions
docs/adr/                accepted/rejected decisions and evidence
```

Create directories as they become useful; this is a boundary map, not a requirement to generate empty abstractions. Keep exact dependency versions in a lock/baseline manifest, not floating prose.

## M0 go/no-go checklist

- A correct JPEG opens on both platforms through the proposed production-shaped decode/render path.
- 60 MP viewing, cancellation and worker failure do not freeze the UI or exceed admitted memory.
- 1:1 physical pixel mapping and the initial SDR color transform are demonstrably correct.
- Traversal/filter semantics are deterministic and tested independently of UI.
- Frame/startup timing has repeated measured evidence; missed goals have an explicit plan or scope correction.
- Frameless controls, keyboard access and recovery are usable on supported window systems.
- Toolkit/codec licenses and build distribution are viable for the intended open-source license.
- Unsupported platform/format capabilities are documented honestly.
- The next milestone has bounded issues and an updated estimate.

Only then should the application shell and feature inventory expand in earnest.
