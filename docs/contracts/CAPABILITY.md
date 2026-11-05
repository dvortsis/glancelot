# CAPABILITY contract

Status: **frozen** by PHASE2-CONTRACTS. Code references are at `b1d57cd`. Owner: Track D. Implementing packet: D2 (F043). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md). Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) section 6.

## Purpose

Give one place that states what this build, this machine and this source can actually do, with **unknown** as an explicit answer. No UI text may claim a capability the app has not established.

## Consumers

| Consumer | Uses |
| --- | --- |
| D2 (F043), after D1 | Implements the model and all four populators; adds the Settings section and the HUD lines; removes the fixed strings below; wires the reference-window check (G-5). |
| D4 (F044 per-monitor ICC) | Writes the main-canvas output-transform item when it lands; reads display ICC availability. |
| E4 (F061 recovery) | Missing codec (`decoder.<format>` unsupported), unreachable source (`source.reachable`), for retry, skip and open-folder messages. |
| F2 (F073, F075) | `source.serverPath` and `source.readOnly` indicators; remote status. Reads the model; does not populate it. |
| Track B (delete, rename, copy, move) | Disable mutation controls with the read-only reason. **Display only:** services still recheck at operation time. |
| E2 (F058 diagnostics) | Includes the capability text in the redacted diagnostics export. |
| A7 (offline-mount marking) | Optional reader of `source.reachable`. |
| F1 (F079) | Sets `GLANCELOT_DEPLOYMENT=container` in the `Dockerfile` runtime `ENV` (line 35). F1 depends on E1, so until it lands, a Docker session reports `serverPaths` false and `display.referenceWindow` `Supported`, today's implicit behaviour. |

**Boundary with DECODE-TIER:** per-image facts (tier, preview size, source dimensions, final or not) come from `PixelBufferDescriptor` and the controller's `imageTier`. This model holds **system and source** facts only, plus per-image *colour source* facts that the worker already reports.

## Current state it replaces

- `contracts.hpp` declared `DisplayCapabilities` and `DecoderCapability`, which nothing used. PHASE2-CONTRACTS deleted them.
- Fixed claims:
  - `ui/SettingsDialog.qml:845`, "JPEG and PNG, displayed as SDR in sRGB…";
  - the status suffix " · display profile not yet managed" (`viewer_controller.cpp:1001`, `:1047`).
- `MetadataParser` defaults `record.bitDepth = 8` and `record.colorSpace = "sRGB"` (`metadata_parser.cpp:550-551`). `colorSpace` is then overwritten when EXIF ColorSpace (`:297-298`) or an ICC profile (`:598`) is found, but `bitDepth` is never parsed. The HUD must show bit depth as unknown, and colour space as unknown when neither source was found, until a parser reads them.
- `DisplayInfo.colorSpace` is already `"Unknown"` when the platform reports none. `output_color::build` (`src/render/output_color.hpp:64`) already returns honest fallback text for the presentation window.
- The F040 reference window (`ui/Main.qml:2115`) opens wherever the platform allows, with no check or message for a session where a second top-level window cannot be shown or placed (STACK-CONFIRM G-5).

## Frozen interface

### In the tree now: `include/glancelot/capability_model.hpp`

`CapabilitySection`, `Support`, `Evidence`, `CapabilityItem`, `CapabilitySnapshot`, `CapabilityModel` (declared; D2 defines it), and the G-5 pieces `kReferenceWindowCapabilityId`, `SecondaryWindowFacts`, `referenceWindowSupport` and `kReferenceWindowExposeDeadlineMs`. Unknown is every enum's and struct's default.

### Other shared-file changes

- `contracts.hpp`: `DisplayCapabilities` and `DecoderCapability` deleted (done).
- `src/app/viewer_context.hpp`: D2 adds `capabilities` to `ViewerContext`, the single wiring point used by `app/main.cpp:269` and `tests/ui_test_support.hpp:82`. `viewer_controller` is not edited. The lead resolves the line.

## Who populates each section, and when

All populators are D2 library classes under `src/app/capability_*.{hpp,cpp}`, in the viewer sources (`cmake/ViewerSources.cmake`), because `glancelot_platform` doesn't link `Qt6::GuiPrivate`. One builder owns each (section, scope) and resends the whole section on any change, so a per-image update keeps `colour.output.*`.

| Section | Items (examples) | Source | When | Evidence |
| --- | --- | --- | --- | --- |
| Decoder | `decoder.<fmt>.still` for every allow-listed format; `.animated`, `.multiPage`, `.highPrecision`; `decoder.raw` = Partial "embedded preview only"; `decoder.qt` version | New worker mode `glancelot-decoder --capabilities`: `QImageReader::supportedImageFormats()` ∩ the single allowlist (DECODE-TIER), plus a static sub-feature table | Once per process, deferred until after the first image is shown or 3 s after start (off the PERF-02 path); 5 s deadline | RuntimeProbe (still), BuildFact (sub-features) |
| Colour | `colour.working` "sRGB 8-bit"; `colour.embeddedProfiles` Supported; `colour.output.main` Unsupported "no monitor transform on the main canvas" until D4; `colour.output.presentation` from `output_color::build` status; `colour.hdr` Unsupported; `colour.physical` Unknown "needs calibrated measurement" | Build facts; `DisplayManager::outputColorStatusChanged`; D4 later | Startup; on output status change; on screen change (scope = screen id) | BuildFact, RuntimeProbe |
| Colour, current image | `colour.source` "Embedded profile converted to sRGB" or Assumed "Untagged source assumed sRGB"; `colour.sourceBitDepth` Unknown until parsed | `viewer.imageColour` (DECODE-TIER): D1 structures the worker's `decoded.color` field (`src/worker/main.cpp:216`, `:265`) into `tagged` and a description | Read on `imageChanged`, so the controller's apply rule has already fenced it | RuntimeProbe or Assumed |
| Display | `display.<id>.dpr`, `.refresh`, `.colorDepth`, `.colorSpace` (Unknown when the platform says "Unknown"), `.icc` (Windows `output_color::systemProfile` result; Linux Unknown); `display.renderer` (graphics API; Partial "software rendering" for WARP or `LIBGL_ALWAYS_SOFTWARE`); `display.referenceWindow` (below) | `DisplayManager::displays()`, `QQuickWindow::rendererInterface()`, ICC probe on the existing single output-colour worker permit | Startup, `displaysChanged`, window `screenChanged` | RuntimeProbe |
| Source | `source.readOnly` (`FILE_READ_ONLY_VOLUME` / `ST_RDONLY`, reusing the probe in `src/core/file_operations.cpp:121`, `:167`); `source.storage` (`IoScheduler::effectiveStorageType`); `source.reachable`; `source.serverPath` | Worker job admitted at `JobPriority::Metadata` (0 bytes); 2 s deadline | Each directory change, per volume root (cached by root key, at most 64 roots) | RuntimeProbe |
| Source, deployment | `source.serverPath` Supported "Paths are on the server, not this computer" | `GLANCELOT_DEPLOYMENT=container`, set by the Dockerfile; absent means local, Unsupported. `serverPaths()` reads this process fact, set once, so it doesn't drop when `setSourceScope` clears the section | Startup | BuildFact |

Rules:

- Never guess host paths; heuristics such as "path starts with /photos" are forbidden.
- No probe on the GUI thread: every volume, ICC and decoder probe is a worker or worker process, because UNC paths can stall.
- Bounded: one source probe in flight, and a newer directory replaces the queued one.
- Each window slot is its own process with its own model and probes (`window_slot.hpp`). One short deferred decoder probe per window is accepted.

## Reference window check and unsupported state (STACK-CONFIRM G-5)

The check is `referenceWindowSupport(SecondaryWindowFacts)` over three facts, read once at startup and again on `displaysChanged`:

- `multipleWindows`: `QGuiApplicationPrivate::platformIntegration()->hasCapability(QPlatformIntegration::MultipleWindows)`. `Qt6::GuiPrivate` is already linked by the app and the UI test targets. D2's populator file is the only new user;
- `canPosition`: `!QGuiApplication::platformName().startsWith("wayland")`, the same rule as `DisplayManager::displaysVariant` (`display_manager.cpp:350`). D2 moves that rule into one helper both use (it lives in the viewer sources, with the populator);
- `containerDeployment`: `GLANCELOT_DEPLOYMENT=container`. The Docker session is one TigerVNC desktop under `openbox` (`docker/start.py:58`) and is recorded as unsupported for multi-output (`LEAD-WRAPUP-2026-10-22.md`).

| Result | Item `display.referenceWindow` | What the user sees |
| --- | --- | --- |
| `Unsupported` | detail "This display system shows one window at a time." | `reference-pin` opens nothing: `viewer.pinReference()` is not called, the notice is "This display system can't show a second window, so the reference photo can't open." The command stays listed. Nothing else changes: the photo and the output are untouched |
| `Partial`, container | detail "Opens on the same remote desktop. It can't be moved to another monitor." | The window opens as today, with this detail in Settings |
| `Partial`, Wayland | detail "Your compositor decides where it opens." | The window opens as today, with this detail in Settings |
| `Supported` | detail empty, evidence `RuntimeProbe` | As today. This never claims physical multi-monitor qualification, which stays a RUNBOOK item |
| `Unknown` (the model isn't populated yet) | | As today; the runtime check below still applies |

**Runtime check.** A platform can claim the capability and still fail to show the window. After a pin, if the window's `visibility` is still `Window.Hidden` once `kReferenceWindowExposeDeadlineMs` has passed, `Main.qml` calls `viewer.clearReference()` and shows "The reference window couldn't open on this display system." The model's item becomes `Unsupported` with evidence `RuntimeProbe` for the rest of the process, and a later `displaysChanged` rebuild keeps it. Clearing releases the retained frame and the canvas's texture reservation through the existing path.

D2's brief grants the `reference-pin` handler (`Main.qml:755`) and the `referenceWindow` block (`Main.qml:2112-2190`) for this. The strings go through `qsTr` and into `translations/glancelot_en_XA.ts`.

## How Settings and the metadata HUD read it

- **Settings (D2):** a "Capabilities" section lists `section("decoder")`, `section("colour")`, `section("display")` and `section("source")` with the label, the detail and a text support badge. The badge is never colour-only and uses theme tokens and the ui-design-skills. It replaces `SettingsDialog.qml:845`. `ui/MonitorSettings.qml:90` keeps showing `outputColorStatus`; the same value also appears as `colour.output.presentation`.
- **Metadata HUD (D2):** per-image lines combine `viewer.imageTier` and the descriptor (DECODE-TIER) with `colour.source` and `colourSummary` for the window's screen. Bit depth shows "unknown" until parsed, and colour space shows "unknown" when the parser found neither EXIF nor ICC.
- **Status text:** " · display profile not yet managed" is removed. The status shows the tier label and the source-colour detail. The output-transform state lives in the HUD and Settings.
- **F075 / Track B:** a read-only badge and disabled mutation actions with the reason when `sourceReadOnly == "yes"`. `"unknown"` leaves actions enabled, and the service's own check decides.
- **`docs/SUPPORTED-CAPABILITIES.md`** stays hand-written. `exportText()` serves E2's diagnostics, not that page.

## Threading, fencing and bounds

- **GUI thread:** `CapabilityModel` and `apply()`. **Workers:** probes. Delivery is by `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- **Fencing:**
  - each snapshot carries `(section, scope, revision)`, and `apply` drops a snapshot whose scope is no longer current or whose revision is not newer;
  - the per-image colour item is also checked against the selection generation by its populator before delivery;
  - a directory change calls `setSourceScope` first, so a late probe for the old folder cannot mark the new folder read-only or writable.
- **Bounds:** `kMaxItems`, `kMaxItemsPerSection`, `kMaxDisplays`, 64 cached volume roots, one probe in flight per section, worker deadlines as stated.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Decoder probe misses its 5 s deadline, the worker is missing, or its JSON is malformed or oversized | The worker is killed; Decoder items stay Unknown with the reason |
| Source probe misses its 2 s deadline | Unknown, "could not check". Actions stay enabled and the service decides |
| Directory change during a source probe | The old probe's result is dropped by scope; a newer directory replaces the queued probe |
| ICC or colour worker fails | The item is Unknown with `output_color`'s fallback text |
| Snapshot over `kMaxItemsPerSection` | Truncated, with an `<section>.truncated` item |
| Reference window unsupported or never exposed | The states above; the selected image, the output and the budget are unchanged |
| Shutdown | Probes capture only by value; pending results are discarded with the model |

## Migration without behaviour change

1. The contract commit added the header and deleted the two unused structs. Nothing else referenced them.
2. D2 adds the model, the populators and the context property. QML that does not bind it is unaffected.
3. String removals happen in D2 together with their replacements, in one packet, so no state ever lacks the information.
4. The worker gains `--capabilities` as a new argv mode. The existing modes and argv forms are unchanged.
5. The Dockerfile environment variable is additive. Without it, `serverPaths` is false, which is today's implicit behaviour.
6. The reference check changes behaviour only on platforms that cannot show a second window. Every gate platform keeps today's behaviour.

## Acceptance tests D2 must pass

New `tests/capability_tests.cpp` (model, no display) and `tests/capability_ui_tests.cpp` (Settings and HUD bindings), through a lead-provided `cmake/CapabilityTests.cmake`.

- **Model:**
  - an absent id reads "unknown";
  - a stale revision is dropped;
  - a wrong scope is dropped;
  - `setSourceScope` resets Source to Unknown;
  - a snapshot over `kMaxItemsPerSection` is truncated with the `.truncated` item;
  - `exportText` redacts source paths;
  - roles and the section filter work.
- **Decoder probe:**
  - worker `--capabilities` against the real build lists exactly the intersection of the allowlist and the plugins;
  - a worker that hangs is killed at the deadline (Unknown with reason);
  - a missing worker gives Unknown with reason;
  - malformed or oversized JSON gives Unknown and does not crash.
- **Source probe:**
  - a writable temp folder gives `readOnly = no`;
  - a read-only mount is gated by `GLANCELOT_READONLY_FILE`, like `file_operations_tests`, and reported **UNVERIFIED** when absent;
  - a slow probe (injected delay) past the deadline gives Unknown;
  - folder A's probe arriving after navigation to folder B is dropped;
  - the env variable set gives `serverPaths` true, and unset gives false, and it stays true across a folder change and a probe timeout.
- **Display and colour:**
  - an injected `DisplayInfo` with colorSpace "Unknown" yields an Unknown item;
  - an output status change updates `colour.output.presentation`;
  - `colour.source` follows `viewer.imageColour`, so a result the controller dropped for generation N after N+1 changes nothing;
  - a per-image update keeps `colour.output.*`;
  - generated tagged (P3) and untagged images give Supported and Assumed.
- **Reference window (G-5):**
  - `referenceWindowSupport` for each row of the table (already asserted in `tests/contract_headers_tests.cpp`; D2 keeps those);
  - the real probe reports `multipleWindows` true on `windows`, `xcb` and `offscreen`, so no gate platform regresses (`wayland` is a RUNBOOK item);
  - a fake `Unsupported` makes `reference-pin` show the notice with no window opened, no `setReferenceImage` call and the current image, output mode and retained bytes unchanged;
  - a test hook that keeps the window hidden past the deadline clears the reference, shows the notice, returns retained bytes to their prior value and marks the item `Unsupported`;
  - a runtime `Unsupported` survives a `displaysChanged` rebuild;
  - the existing `reference_pin` and `reference_pin_native` tests pass unchanged.
- **UI:**
  - Settings lists all four sections;
  - no fixed "SDR in sRGB" claim remains (string search in the test);
  - the read-only badge appears only for "yes";
  - badges carry text as well as colour.
- **Hardware:** ICC, HDR and refresh accuracy remain **unverified** RUNBOOK items. No test promotes `colour.physical` above Unknown.

## Not covered

- Authorization: read-only status never gates a service; `FileOperationService` and the other services recheck.
- Per-image tier and dimension facts (DECODE-TIER); metadata parsing of bit depth and profile names (D6 needs bit depth for clipping and owns that parser; D2 shows it as unknown until then).
- Applying monitor colour transforms (D4); HDR output (COLOR-04); physical qualification (RUNBOOK packets only).
- Multi-monitor placement of the reference window and restoring it after restart (`docs/F040-PLAN.md`, "Not done").
- Remote quality presets and connection timing (F2 itself); power profiles (E3).
- Persisting capabilities across runs; telemetry or any network reporting (forbidden).
