# Qualification operator guide

qualification.py emits schema 1 with passed, failed and unverified rows. It verifies
the package inventory and binds smoke evidence to the exact manifest, executable
hashes, source and target. PNG/JPEG dimensions, an actual source-color screenshot,
package-only Qt modules, missing-decoder diagnosis and unchanged generated-media
hashes are required. Keep generated-quadrants.png and generated-smoke.jpg beside
smoke.json. Keep the independent dependency report for Linux.

```text
python tools/platform/qualification.py --package PACKAGE --smoke SMOKE/smoke.json --dependencies dependencies.json --repeat REPEAT/comparison.json --output NEW_MATRIX.json --case C-CPU
```

Omit --dependencies on Windows. Omitting --repeat leaves compiler/archive
reproducibility unverified. Supplying it reopens and compares both actual archives,
requiring the same source, target, configuration and epoch as the qualified package.
Archive paths in comparison.json are relative to that report. Dependency evidence
must cover every unique packaged ELF digest; Qt/ICU must resolve inside the package.
Reports are local integrity evidence, not signed attestations against a hostile
report author. Container exports include qualification/matrix.json, dependencies,
smoke, generated media and screenshot. Recheck them against the exported package.

--case declares the intended reference scenario; it does not certify its hardware.
OS/Python/visible CPU count, observed graphics API, DPR and first image frame time
are recorded separately. Power/thermal/cache state and actual active GPU identity
remain uncontrolled or unverified. Timings from tiny generated images do not
qualify 12/24/45/60 MP decode limits or controlled cold-cache performance.

The following require an operator and independent review; the script never closes
them from a software screenshot:

- Windows clean machine: extract on a separately provisioned supported Windows
  x64 host without Qt/development tools. Record OS/runtime prerequisites, package
  hashes and launch/decode/failure evidence. Development PATH isolation alone is
  insufficient.
- Native Linux: record distro, compositor, session protocol and driver. Test X11
  and Wayland independently. This pinned package ships xcb, not QtWayland; XWayland
  is an X11 result. Ubuntu container/software GL is only that environment.
- DPI: record named physical monitors, resolution/scaling at 100/125/150/200 percent,
  window transitions, clipping and pointer/keyboard behavior.
- Refresh: record actual 60/144 Hz monitors, input/frame traces and dropped frames.
  Measure 240 Hz only when available. A requested setting is not observed refresh.
- ICC/HDR: record calibrated reference, profile/output mode, reference patches and
  measured error. Pure RGB screenshot counts establish content presence only.
- R1: named 16 GiB/NVMe/iGPU system and versioned 12/24/45/60 MP fixtures.
- R2: actual 8 GiB/HDD or explicitly documented storage throttling.
- R3: named discrete GPU with mixed DPI/display transitions.
- R4: controlled SMB source, disconnect/recovery/cancellation and source hashes.

Preserve CANVAS-UI, COLOR-01 and DECODE-01 acceptance criteria. Save raw captures,
fixture manifests, configuration and terminal results with each reviewed outcome.
Do not flush caches, change machine settings or install drivers automatically.
