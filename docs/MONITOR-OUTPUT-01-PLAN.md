# MONITOR-OUTPUT-01: Second Display Presentation Window Lifecycle

## 1. Requirements & Objectives
- **Target Requirements**:
  - `UX-10`: Controller plus clean second-monitor presentation. Controller UI (tree, grid, metadata, dialogs) never appears on output; output-specific scale and color; monitor unplug and focus changes recover safely. Explicit display choice, mirror/pinned-reference/comparison routing, clean fullscreen/windowed output, linked or independent viewports, backgrounds/overlays/cursor customization, named presets, per-slot persistence and always-reachable recovery; missing display identities never restore controls offscreen.
  - `COLOR-01`: Current-monitor ICC awareness and transition handling. SDR reference patches; per-screen DPR and color space handling; fallback to sRGB when profile is unavailable.
  - `VIEW-04`: 100% means one image pixel per physical display pixel. Honors presentation display's native device pixel ratio and geometry.
  - `PERF-04`: Resource bounds. Shared decode between controller and presentation display (no duplicate raster decodes); bounded aggregate CPU/GPU memory footprint.
  - `SYS-02`: Robust resource and lifecycle boundaries. Window close / Escape key dismisses presentation cleanly; closing output never exits controller and never deletes or mutates source files. Disconnect/hotplug preserves navigation and releases obsolete resources.

## 2. Shared Contracts & Architecture

### A. Display Enumeration & Capabilities (`glancelot::DisplayInfo`)
```cpp
namespace glancelot {

enum class PresentationMode {
    Mirror,      // Follows current controller image
    Pinned,      // Locks to a specific pinned photo
    Comparison   // Comparison display (side-by-side or difference)
};

struct DisplayInfo {
    int index{-1};
    QString id;                 // Stable identity hash/string
    QString name;               // Readable display name (e.g., "Dell U2720Q", "Display 2")
    QString manufacturer;
    QString model;
    QString serialNumber;
    QRect geometry;             // Virtual geometry
    QRect availableGeometry;    // Work area geometry
    QSize physicalSizeMm;       // Physical dimensions in mm
    qreal devicePixelRatio{1.0};
    qreal refreshRate{60.0};
    int colorDepth{24};
    QString colorSpace{"sRGB"};
    bool isPrimary{false};
    bool isAvailable{true};
};

} // namespace glancelot
```

### B. Stable Identity Resolution
- Where available, display identity combines manufacturer, model, serial number, and physical dimensions.
- If EDID/hardware serial is unavailable or generic, fall back conservatively to screen connector name + geometry hash, labeled as a heuristic identity.
- When an identified display disconnects, its numeric index is NOT automatically reassigned to a different monitor.

### C. DisplayManager Lifecycle (`glancelot::DisplayManager`)
- Inherits `QObject`.
- Maintains active list of displays, subscribing to `QGuiApplication::screenAdded`, `QGuiApplication::screenRemoved`, and `QGuiApplication::primaryScreenChanged`.
- Tracks presentation window state:
  - `bool isPresentationActive()`
  - `PresentationMode mode()`
  - `QString activeDisplayId()`
  - `QString currentImagePath()`
  - `QString pinnedImagePath()`
  - `quint64 currentGeneration()`
- Methods:
  - `QList<DisplayInfo> displays() const;`
  - `DisplayInfo preferredPresentationDisplay() const;` (picks first non-primary screen if multi-monitor; or primary if single-monitor)
  - `bool openPresentation(const QString& displayId = QString(), PresentationMode mode = PresentationMode::Mirror);`
  - `void closePresentation();`
  - `void togglePresentation();`
  - `void setMode(PresentationMode mode);`
  - `void updateImage(const QString& path, const QImage& image, quint64 generation);`
  - `void pinImage(const QString& path, const QImage& image, quint64 generation);`
  - `void unpinImage();`
- Safety:
  - If target display is unplugged while presentation is active, gracefully close the presentation window or relocate safely to primary display, setting status and firing `presentationClosed()`.
  - Closing presentation window leaves controller untouched.
  - `Escape` key inside presentation window immediately dismisses it.

### D. Clean Presentation Window (`glancelot::PresentationWindow`)
- Top-level `QQuickWindow` with an embedded `ImageCanvas` (QQuickRhiItem GPU renderer).
- Fullscreen on target screen (or windowed when configured / during testing).
- Frameless, zero chrome: no menus, toolbars, HUDs, or dialogs.
- Background defaults to `#000000` (true black for presentation).
- Intercepts `Qt::Key_Escape` to invoke `close()`.

### E. Controller Integration & Commands
- Commands in `ViewerController`:
  - `presentation-toggle`: "Toggle second display presentation" (default `F5`).
  - `presentation-mirror`: "Presentation: Mirror current photo" (default `""`).
  - `presentation-pin`: "Presentation: Pin current photo" (default `""`).
  - `presentation-compare`: "Presentation: Comparison mode" (default `""`).
  - `presentation-close`: "Close presentation window" (default `""`).
- QML Properties on `ViewerController`:
  - `presentationActive` (bool)
  - `presentationMode` (QString)
  - `presentationDisplays` (QVariantList)
  - `presentationTargetDisplay` (QString)

## 3. Verification Plan
- Unit tests in `tests/monitor_output_tests.cpp`:
  1. Display capability enumeration (primary screen, geometry, DPR, refresh rate, color space).
  2. Stable identity hashing and fallback handling.
  3. Presentation window open/close lifecycle and toggle.
  4. Routing mode transitions (Mirror -> Pinned -> Comparison -> Mirror).
  5. Stale generation rejection (older generation image cannot overwrite newer generation).
  6. Pinned mode persistence across image navigation (controller image updates, pinned image stays locked).
  7. Escape key dismissal.
  8. Display disconnect / hotplug handling.
  9. Controller command palette registration and trigger dispatch.
- Integration tests:
  - Windows native CTest (`scripts/build.ps1 -Test`).
  - Linux Docker build (`docker build -t glancelot-browser:main .`).

