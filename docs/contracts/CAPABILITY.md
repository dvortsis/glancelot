# CAPABILITY contract proposal (draft for lead freeze)

Status: **proposal**, not frozen. Base: `origin/main` `3893c77`. Owner after freeze: Track D (D2). Plan: [LEAD-PLAN-2026-10-22.md](../handoff/LEAD-PLAN-2026-10-22.md) §6.

## 1. Purpose and consumers

Give one place that states what this build, this machine and this source can actually do, with **unknown** as an explicit answer. The goal is that no UI text claims a capability the app has not established.

| Consumer | Uses |
| --- | --- |
| D2 (F043) | Implements the model and all four populators; adds the Settings section and the HUD lines; removes the fixed strings in §6. |
| D4 (F044 per-monitor ICC) | Writes the main-canvas output-transform item when it lands; reads display ICC availability. |
| E4 (F061 recovery) | Missing codec (`decoder.<format>` unsupported), unreachable source (`source.reachable`), for retry, skip and open-folder messages. |
| F2 (F073, F075) | `source.serverPath` and `source.readOnly` indicators; remote status. |
| B (delete, rename, copy, move) | Disable mutation controls with the read-only reason. **Display only:** services still recheck at operation time. |
| E2 (F058 diagnostics) | Includes the capability text in the redacted diagnostics export. |
| A7 (offline-mount marking) | Optional reader of `source.reachable`. |

**Boundary with DECODE-TIER:** per-image facts (tier, preview size, source dimensions, final or not) come from `PixelBufferDescriptor` and the controller's `imageTier`. This model holds **system and source** facts only, plus per-image *colour source* facts that the worker already reports.

## 2. Current state it replaces

- `contracts.hpp` declares `DisplayCapabilities` and `DecoderCapability`, which nothing uses. This contract **replaces and deletes them** (see open questions).
- Fixed claims:
  - `SettingsDialog.qml:716`, "JPEG and PNG, displayed as SDR in sRGB…";
  - the status suffix " · display profile not yet managed" (`viewer_controller.cpp:921,969`).
- `MetadataRecord.bitDepth = 8` and `colorSpace = "sRGB"` are hard-coded (`metadata_parser.cpp:543-544`), not parsed. The HUD must show them as unknown until a parser actually reads them.
- `DisplayInfo.colorSpace` is already `"Unknown"` when the platform reports none. `output_color::build` already returns honest fallback text for the presentation window.

## 3. Proposed header: new `include/glancelot/capability_model.hpp`

```cpp
#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace glancelot {
enum class CapabilitySection : quint8 { Decoder, Colour, Display, Source };
// Unknown is the default and the honest answer when nothing has established the fact.
enum class Support : quint8 { Unknown, Unsupported, Partial, Supported };
// How the value was established. Runtime code never claims physical qualification: that is RUNBOOK evidence only.
enum class Evidence : quint8 { None, BuildFact, RuntimeProbe, Assumed, UserSetting };

struct CapabilityItem {
    QString id;            // stable dotted id, e.g. "decoder.webp.still", "display.<displayId>.icc", "source.readOnly"
    CapabilitySection section{CapabilitySection::Decoder};
    Support support{Support::Unknown};
    Evidence evidence{Evidence::None};
    QString label;         // translated short name
    QString detail;        // translated user-facing value or reason ("Read-only mount", "Embedded profile converted to sRGB")
    QString value;         // untranslated machine value ("D3D11", "sRGB", "6.8.3"); never a host path guess
};
// One section replaced atomically. `scope` fences per-context sections: Source = directory key; Display/Colour = the
// window's screen id ("" = process-wide). `revision` is the populator's monotonic counter for that section and scope.
struct CapabilitySnapshot {
    CapabilitySection section{CapabilitySection::Decoder};
    QString scope;
    quint64 revision{};
    QList<CapabilityItem> items;
};

// GUI-thread only. Populators build snapshots on worker threads and deliver them by queued call to apply().
// Bounds: at most kMaxItems rows in total, kMaxItemsPerSection per snapshot (extra items are dropped and an
// "<section>.truncated" item is added), kMaxDisplays display scopes retained.
class CapabilityModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString decoderSummary READ decoderSummary NOTIFY sectionChanged)
    Q_PROPERTY(QString colourSummary READ colourSummary NOTIFY sectionChanged)
    Q_PROPERTY(QString displaySummary READ displaySummary NOTIFY sectionChanged)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY sectionChanged)
    // Display-only conveniences for F075 and B. Never authorization: services recheck at operation time.
    Q_PROPERTY(QString sourceReadOnly READ sourceReadOnly NOTIFY sectionChanged)   // "unknown" | "yes" | "no"
    Q_PROPERTY(bool serverPaths READ serverPaths NOTIFY sectionChanged)
public:
    static constexpr int kMaxItems = 256, kMaxItemsPerSection = 96, kMaxDisplays = 16;
    enum Role { IdRole = Qt::UserRole + 1, SectionRole, SupportRole, EvidenceRole, LabelRole, DetailRole, ValueRole };
    explicit CapabilityModel(QObject* parent = nullptr);
    ~CapabilityModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns false (no change) when the scope is not current or the revision is not newer than the stored one.
    bool apply(CapabilitySnapshot snapshot);
    // Current contexts. Changing the source scope clears the Source section to Unknown until a matching snapshot arrives.
    void setSourceScope(const QString& directoryKey);
    void setScreenScope(const QString& displayId);

    Q_INVOKABLE QString support(const QString& id) const;   // "unknown" | "unsupported" | "partial" | "supported"; absent id -> "unknown"
    Q_INVOKABLE QVariantMap item(const QString& id) const;  // {} for an absent id
    Q_INVOKABLE QVariantList section(const QString& name) const; // "decoder" | "colour" | "display" | "source"
    Q_INVOKABLE QString exportText() const;                 // for E2; source paths replaced by "<source>"
    QString decoderSummary() const; QString colourSummary() const; QString displaySummary() const;
    QString sourceSummary() const; QString sourceReadOnly() const; bool serverPaths() const;
signals:
    void sectionChanged(glancelot::CapabilitySection section);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
```

`contracts.hpp` diff: delete `DisplayCapabilities` and `DecoderCapability` (no users).

## 4. Who populates each section, and when

All populators are D2 library classes under `src/app/capability_*.{hpp,cpp}`. The model is created in `app/main.cpp` and installed through the EN-04 shared context helper as `capabilities`. The lead resolves the instantiation line, and `viewer_controller` is not edited.

| Section | Items (examples) | Source | When | Evidence |
| --- | --- | --- | --- | --- |
| Decoder | `decoder.<fmt>.still` for every allow-listed format; `.animated`, `.multiPage`, `.highPrecision`; `decoder.raw` = Partial "embedded preview only"; `decoder.qt` version | New worker mode `glancelot-decoder --capabilities`: `QImageReader::supportedImageFormats()` ∩ the single allowlist (DECODE-TIER §4.5), plus a static sub-feature table | Once per process, deferred until after the first image is shown or 3 s after start (off the PERF-02 path); 5 s deadline; failure leaves Unknown with reason | RuntimeProbe (still), BuildFact (sub-features) |
| Colour | `colour.working` "sRGB 8-bit"; `colour.embeddedProfiles` Supported; `colour.output.main` Unsupported "no monitor transform on the main canvas" until D4; `colour.output.presentation` from `output_color::build` status; `colour.hdr` Unsupported; `colour.physical` Unknown "needs calibrated measurement" | Build facts; `DisplayManager::outputColorStatusChanged`; D4 later | Startup; on output status change; on screen change (scope = screen id) | BuildFact, RuntimeProbe |
| Colour, current image | `colour.source` "Embedded profile converted to sRGB" or Assumed "Untagged source assumed sRGB"; `colour.sourceBitDepth` Unknown until parsed | Worker `decoded.color` field, structured by D1 (`tagged`, profile description) | Per applied image, fenced by selection generation (a stale generation is dropped before `apply`) | RuntimeProbe or Assumed |
| Display | `display.<id>.dpr`, `.refresh`, `.colorDepth`, `.colorSpace` (Unknown when the platform says "Unknown"), `.icc` (Windows `systemProfile` result; Linux Unknown); `display.renderer` (graphics API; Partial "software rendering" for WARP or `LIBGL_ALWAYS_SOFTWARE`) | `DisplayManager::displays()`, `QQuickWindow::rendererInterface()`, ICC probe on the existing single output-colour worker permit | Startup, `displaysChanged`, window `screenChanged` | RuntimeProbe |
| Source | `source.readOnly` (`FILE_READ_ONLY_VOLUME` / `ST_RDONLY`, reusing the probe in `src/core/file_operations.cpp`); `source.storage` (`IoScheduler::effectiveStorageType`); `source.reachable`; `source.serverPath` | Worker job admitted at `JobPriority::Metadata` (0 bytes); 2 s deadline, after which Unknown "could not check" | Each directory change, per volume root (cached by root key, at most 64 roots) | RuntimeProbe |
| Source, deployment | `source.serverPath` Supported "Paths are on the server, not this computer" | Explicit `GLANCELOT_DEPLOYMENT=container` environment variable set by the Dockerfile (Track F request); absent means local, Unsupported | Startup | BuildFact |

Rules:
- Never guess host paths; heuristics such as "path starts with /photos" are forbidden.
- No probe on the GUI thread: every volume, ICC and decoder probe is a worker or worker process, because UNC paths can stall.
- Bounded: one source probe in flight, and a newer directory replaces the queued one.

## 5. Threading, fencing and bounds

- **GUI thread:** `CapabilityModel` and `apply()`. **Workers:** probes. Delivery is by `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- **Fencing:**
  - each snapshot carries `(section, scope, revision)`, and `apply` drops a snapshot whose scope is no longer current or whose revision is not newer;
  - the per-image colour item is also checked against the selection generation by its populator before delivery;
  - a directory change calls `setSourceScope` first, so a late probe for the old folder cannot mark the new folder read-only or writable.
- **Bounds:** `kMaxItems`, `kMaxItemsPerSection`, `kMaxDisplays`, 64 cached volume roots, one probe in flight per section, worker deadlines as stated.
- **Lifetime:** probes capture only by value. At shutdown, pending results are discarded with the model.

## 6. How Settings and the metadata HUD read it

- **Settings (D2):** a "Capabilities" section lists `section("decoder")`, `section("colour")`, `section("display")` and `section("source")` with the label, the detail and a text support badge. The badge is never colour-only and uses theme tokens and the ui-design-skills. It replaces `SettingsDialog.qml:716`. `MonitorSettings.qml` keeps showing `outputColorStatus`; the same value also appears as `colour.output.presentation`.
- **Metadata HUD (D2):** per-image lines combine `viewer.imageTier` and the descriptor (DECODE-TIER) with `colour.source` and `colourSummary` for the window's screen. `bitDepth` and `colorSpace` from `MetadataRecord` show "unknown" until parsed.
- **Status text:** " · display profile not yet managed" is removed. The status shows the tier label and the source-colour detail. The output-transform state lives in the HUD and Settings.
- **F075 / B:** a read-only badge and disabled mutation actions with the reason when `sourceReadOnly == "yes"`. `"unknown"` leaves actions enabled, and the service's own check decides.

## 7. Migration without behaviour change

1. The contract commit adds the header and deletes the two unused structs. Nothing else references them.
2. D2 adds the model, the populators and the context property. QML that does not bind it is unaffected.
3. String removals (§2) happen in D2 together with their replacements, in one packet, so no state ever lacks the information.
4. The worker gains `--capabilities` as a new argv mode. The existing modes and argv forms are unchanged.
5. The Dockerfile environment variable (Track F) is additive. Without it, `serverPaths` is false, which is today's implicit behaviour.

## 8. Test plan

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
  - the env variable set gives `serverPaths` true, and unset gives false.
- **Display and colour:**
  - an injected `DisplayInfo` with colorSpace "Unknown" yields an Unknown item;
  - an output status change updates `colour.output.presentation`;
  - the per-image `colour.source` for generation N arriving after N+1 is dropped;
  - generated tagged (P3) and untagged images give Supported and Assumed.
- **UI:**
  - Settings lists all four sections;
  - no fixed "SDR in sRGB" claim remains (string search in the test);
  - the read-only badge appears only for "yes";
  - badges carry text as well as colour.
- **Hardware:** ICC, HDR and refresh accuracy remain **unverified** RUNBOOK items. No test promotes `colour.physical` above Unknown.

## 9. Open questions for the lead

1. **Container signal.** Approve `GLANCELOT_DEPLOYMENT=container`, set by the Dockerfile (a Track F file), as the only server-path signal? Without it, F075 can't distinguish server paths honestly.
2. **Delete the unused structs.** Delete `DisplayCapabilities` and `DecoderCapability` now (proposed), or keep them until D2 lands?
3. **Generated capability page.** Should `exportText()` (or a CLI flag) generate `docs/SUPPORTED-CAPABILITIES.md` rows per platform (FMT section: "generated capability page")? Or does EN-08 keep that page hand-written?
4. **Ownership split.** D2 owns all four populators (proposed), or does F2 own the Source populator, given that F075 is its feature?
5. **Per-process scope.** Window slots are independent viewer processes (`window_slot.hpp`), each with its own model and probes. Is a duplicate decoder probe per window acceptable (one short worker run each, deferred)?

## 10. Deliberately not covered

- Authorization: read-only status never gates a service; `FileOperationService` and the other services recheck.
- Per-image tier and dimension facts (DECODE-TIER); metadata parsing of bit depth and profile names (a later parser packet).
- Applying monitor colour transforms (D4); HDR output (COLOR-04); physical qualification (RUNBOOK packets only).
- Remote quality presets and connection timing (F2 itself); power profiles (E3).
- Persisting capabilities across runs; telemetry or any network reporting (forbidden).
