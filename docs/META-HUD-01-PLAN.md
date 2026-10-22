# META-HUD-01 Task Packet: Bounded Asynchronous Metadata HUD

**Status**: Ready -> Implementing  
**Owner**: Gemini Lane (`agent/meta-hud`)  
**Worktree**: `.worktrees/meta-hud`  
**Build Dir**: `build-meta-hud`  
**Requirements**: `META-01`, `UX-04`, `UX-05`  
**Dependencies**: `[IO-01]` (Integrated into `main` at `1f97893`)

---

## 1. Objectives & Scope

1. **`META-01` (Categorized EXIF/IPTC/XMP Inspection)**:
   - Provide bounded, lazy asynchronous extraction of image metadata:
     - Standard EXIF tags: Camera Make/Model, Lens, Exposure Time, F-Number, ISO, Focal Length, 35mm Equivalent, Exposure Program, Metering Mode, Flash, White Balance, Date/Time Original, Date/Time Digitized.
     - Image / Technical: Stored vs Orientation-Aware Dimensions, Stored Orientation code & description, Color Space (sRGB, Adobe RGB, Display P3, ICC profile name), Bit Depth, Format/MIME.
     - GPS: Latitude, Longitude, Altitude (formatted DMS and decimal degrees, safe zero-network operation).
     - IPTC: Title/Headline, Caption/Description, Creator/Artist/Byline, Copyright, Keywords.
     - All / Raw tags: Searchable inventory of all discovered tags.
   - Strict safety & robustness:
     - Never read or parse metadata on the UI or render thread.
     - Bounded memory and I/O: read at most header limits (e.g. 256 KB) from disk; prioritize via `IoScheduler` with `JobPriority::Metadata`.
     - Resilient to corrupted, truncated, malformed, or hostile inputs (e.g. circular IFD offset loops, out-of-bounds pointer offsets, integer overflow).

2. **`UX-04` (Right HUD: Metadata Inspector)**:
   - Collapsible/dockable right HUD panel registered in `PaneLayout` as `"metadata"`.
   - Native Qt/QML UI (`ui/MetadataHud.qml`) styled with `Theme.qml` tokens.
   - Category filtering: Summary, Camera, Technical, GPS, IPTC, All.
   - Instant search filter over tag keys, labels, and formatted values.
   - One-click copy for individual values or complete summary.
   - Generation tracking: rapid photo switching cancels or discards stale in-flight parses.

3. **`UX-05` (Bottom HUD: Status & Image Details)**:
   - Basic image status in statusbar: orientation-aware dimensions, file size, current index of total count, and loading states.
   - Command `toggle-metadata` (`Ctrl+I` / `i`) with full keyboard accessibility.

---

## 2. Shared Contracts & Interfaces

### 2.1 `include/glancelot/metadata_model.hpp`
- Defines `MetadataItem` (key, label, value, rawValue, category).
- Defines `MetadataRecord` containing snapshot metadata, generation, and tag collections.
- Defines `MetadataModel : public QAbstractListModel`:
  - Roles: `KeyRole`, `LabelRole`, `ValueRole`, `RawValueRole`, `CategoryRole`.
  - Properties: `filePath`, `loading`, `hasMetadata`, `count`, `searchFilter`, `categoryFilter`, and key getters (`cameraMake`, `cameraModel`, `exposure`, `fNumber`, `iso`, `focalLength`, `dateTime`, `dimensions`, `colorSpace`, `hasGps`, `gpsCoords`, `title`, `copyright`).
  - Methods: `loadForFile(filePath, generation)`, `clear()`, `copySummary()`, `copyGps()`, `copyTag(key)`.

### 2.2 `include/glancelot/viewer_controller.hpp`
- Exposes `metadataModel`: `MetadataModel* metadataModel() const;`
- Exposes `metadataVisible`: `bool isMetadataVisible() const; void setMetadataVisible(bool);`
- Registers command `toggle-metadata` ("Toggle Metadata Inspector", shortcut `i` / `Ctrl+I`).

### 2.3 `include/glancelot/pane_layout.hpp`
- Adds `"metadata"` pane to `defaults()` with `region: "right"`, `extent: 320`, `visible: false`, `frozen: false`.

---

## 3. Test & Verification Plan

1. **Dedicated Unit Tests (`tests/metadata_tests.cpp`)**:
   - `testValidJpegExif`: Camera, exposure, lens, and date parsing.
   - `testOrientationAndDimensions`: Stored orientation and orientation-adjusted dimensions.
   - `testGpsCoordinates`: Latitude, longitude, altitude parsing and formatting.
   - `testIptcMetadata`: Title, creator, copyright, and keywords.
   - `testPngAndWebpMetadata`: Text chunks and EXIF payload extraction.
   - `testMalformedHeaderResilience`: Truncated, invalid IFD offsets, and garbage payloads do not crash.
   - `testCircularIfdLoopPrevention`: IFD pointing to previous IFD offset is safely aborted.
   - `testGenerationValidation`: Rapid sequential requests; only latest generation emits and updates model.
   - `testSearchAndCategoryFilter`: Filtering tags by category and query string.
   - `testCopySummaryAndTag`: Clipboard formatted strings.

2. **Integration Verification**:
   - Full Windows native suite (`scripts/build.ps1 -Test`).
   - Linux Docker container verification (`docker build -t glancelot-browser:main .`).
   - Recorded evidence in `docs/verification/META-HUD-01.md`.

