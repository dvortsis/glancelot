# COMPARE-PANES contract proposal (draft for the lead)

Status: **draft, not frozen**. Proposed header: `include/glancelot/compare_panes.hpp` plus a diff to `include/glancelot/pane_layout.hpp`. Base: `origin/main` at `3893c77`. Depends on RESOURCE-LEDGER.

## 1. Purpose and consumers

Bring 2-up and 4-up comparison into the main window (ADV-01): linked or independent pan/zoom, an active candidate, candidate replacement and removal. Memory budgets include every pane, and **removing a pane never deletes a file**. The out-of-window precedent is the presentation comparison: `IPresentationWindow::setComparisonMode/setPrimaryImage/setSecondaryImage/viewportState/restoreViewport/viewportCommand`, two canvases sharing one `CanvasTextureAdmission`, with the texture budget split in half. This contract generalizes that to N ≤ 4 panes, in-app, without changing the presentation window.

Consumers: **C3** (COMPARE-PANES + F026), **C4** (blink, wipe, difference over the same panes and transforms), **Track D** canvas (D1 fit tier for panes, D3 texture reuse, D6 sampling per pane), **E1** (ledger owners), **E6** (accessibility of pane focus).

## 2. Proposed header: `include/glancelot/compare_panes.hpp`

```cpp
#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace glancelot {
// Lead-frozen COMPARE-PANES. GUI thread only. Compare never decodes and never touches files: it asks
// the window's ViewerController to decode through the ordinary decode/cache path and receives values.
inline constexpr int kMaxComparePanes = 4;
enum class CompareLayout : quint8 { Off, Two, Four };        // PaneLayout contentMode "compare2"/"compare4"
enum class PaneState : quint8 { Empty, Loading, Ready, Failed, Refused /*ledger*/, TooLarge /*per-pane GPU*/ };

// In-app counterpart of IPresentationWindow's comparison surface; implemented by the QML compare host
// (C3) and by a fake in tests. Fit modes, viewport keys and commands use the presentation vocabulary.
class ICompareSurface {
public:
    virtual ~ICompareSurface() = default;
    virtual void setLayout(CompareLayout) = 0;
    virtual void setPaneImage(int pane, const QImage& image, quint64 generation) = 0; // null clears
    virtual void setActivePane(int pane) = 0;
    virtual void setFitMode(const QString& mode) = 0;
    // {"panes":[ImageCanvas::viewState()...], "linked":bool, "active":int}
    virtual QVariantMap viewportState() const = 0;
    virtual void restoreViewport(const QVariantMap& state) = 0;
    // "actual" | "rotate" | "linked" | "fit:<mode>" | "zoom-in" | "zoom-out" | "pan:<dx>,<dy>"
    virtual void viewportCommand(const QString& command) = 0;
    virtual qint64 admittedTextureBytes() const = 0;            // the window's canvas-group owner view
};

// Two-pane presentation state <-> compare state; lets "send comparison to presentation" reuse both.
QVariantMap compareStateFromPresentation(const QVariantMap& presentation); // primary/secondary/linked/activeSecondary
QVariantMap presentationStateFromCompare(const QVariantMap& compare);      // panes[0..1] only

class CompareSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int paneCount READ paneCount NOTIFY changed)              // 0, 2 or 4
    Q_PROPERTY(int activePane READ activePane WRITE setActivePane NOTIFY changed)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY changed)
    // [{pane, path, name, state, tier, message}] - values only, no QImage in QML
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
public:
    using GenerationSource = std::function<quint64()>;   // the controller's monotonic selection space
    explicit CompareSession(GenerationSource generations, QObject* parent = nullptr);
    ~CompareSession() override;
    int paneCount() const; int activePane() const; bool linked() const; QVariantList candidates() const;
    void setActivePane(int pane); void setLinked(bool linked);

    Q_INVOKABLE bool begin(int panes, const QStringList& paths);   // 2|4; paths.size() <= panes; files only
    Q_INVOKABLE bool setCandidate(int pane, const QString& path);  // replace; cancels the pane's request
    Q_INVOKABLE bool replaceActive(const QString& path);
    Q_INVOKABLE void swap(int a, int b);
    // Drops the pane's image, request and reservations. Never a file operation, never a confirmation.
    Q_INVOKABLE void removeCandidate(int pane);
    Q_INVOKABLE void end();                                         // releases everything to baseline

    // Controller side. Results whose generation is not the pane's current one are dropped silently.
    void deliver(int pane, quint64 generation, const QImage& image, const QString& tier);
    void fail(int pane, quint64 generation, PaneState state, const QString& message);
signals:
    void changed();
    void decodeRequested(int pane, const QString& path, quint64 generation, bool active);
    void cancelRequested(int pane, quint64 generation);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
```

**Diff to `include/glancelot/pane_layout.hpp`:**

```diff
-    Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)   // "preview" | "grid" | "combined"
+    // "preview" | "grid" | "combined" | "compare2" | "compare4". Compare modes are session-only:
+    // never persisted, never saved in presets or monitor snapshots (baseContentMode is stored instead).
+    Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)
+    Q_PROPERTY(QString baseContentMode READ baseContentMode NOTIFY changed) // mode restored on exit
 ...
+    QString baseContentMode() const;
+    Q_INVOKABLE bool exitCompare();                                    // contentMode = baseContentMode
 private:
+    QString baseMode_{QStringLiteral("preview")};
```

`modeValid` in `pane_layout.cpp` accepts the compare values for `setContentMode` only. `load()`, `persist()`, `snapshot()`, `savePreset()` and the `settings_policy.cpp` / `monitor_customization_state.hpp` validators keep the three persisted values, so older binaries and monitor presets are unaffected. Like `combined`, `setContentMode("compare*")` is a view preference, allowed under the global lock and with frozen center panes.

**Command scope** (`contracts.hpp` comment, IN-02): add `"compare"` to the documented scopes. C3 registers its commands in its own block of `command_registry.cpp`.

## 3. Semantics

**Entering and leaving.** `begin()` comes from the grid/filmstrip selection (2 to 4 files) or "compare with current". The main preview canvas binds an empty image while compare is shown, so it releases its texture. Pane 0 shows the current image, which avoids a second texture of the same pixels in the same window. `end()` or `exitCompare()` restores `baseContentMode` and the previous selection, and releases every pane reservation.

**Decoding.** Each pane request gets a generation from the controller's selection-generation space (the `DisplayManager::updateImage` rule: no independent per-producer counter). The controller serves `decodeRequested` through its normal path, decoded cache first. The active pane runs at `JobPriority::Foreground`, other panes are queued behind it one at a time (Visible), and at most one compare decode runs besides the current-image decode. `setCandidate`, `removeCandidate`, `swap` and `end` cancel stale requests. A late `deliver` for an old generation is dropped and its reservation released.

**Linking.** `Linked` matches the presentation's `m_linked` behaviour: one pane's `viewState()` (physical scale and centre) is copied to the others on every change. `Independent` keeps each pane's own state. Rotation and fit follow the presentation rules (`viewportCommand`). C4 reuses the aligned transforms.

**Ledger (every pane counted).**
- CPU: one owner per window, `compare:<slot>`. Each pane holds a CpuPixels `Reservation` with `shareKey = image.cacheKey()`, so a pane showing the current or a cached image is charged once (shared). Four distinct images make four charges.
- GPU: all pane canvases share one canvas group (`shareTextureAdmissionWith`), whose GpuTextures owner quota equals the main canvas budget. As in `PresentationWindow::setComparisonMode`, each pane's `textureBudget` is `aggregate / paneCount` (2-up 128 MiB, 4-up 64 MiB per pane).
- A pane whose image exceeds its share becomes `TooLarge` with honest text ("Too large for 4-up; use 2-up or single view") until D1 provides a labelled fit tier. It is never silently downscaled on the UI thread.
- A ledger refusal makes that pane `Refused`. Other panes and the current image are untouched. Under pressure the inactive panes' textures are reclaimed at tier Hidden only when the window is inactive.

**Removal never deletes.** `removeCandidate` is not wired to any `FileOperationService`, `CopyMoveService` or `MutationService` path, and holds no reference to them. In compare scope, `Delete` and `Backspace` are bound to `compare-remove-pane`. Deleting the active candidate's file requires the explicit delete command (palette/menu), which opens the normal FS-01 confirmation for that one file (open question 1).

**Threading and bounds.** `CompareSession` and `ICompareSurface` are GUI-thread only. QML gets value lists, never `QImage`. Limits: at most 4 panes, one pending request per pane, one compare decode in flight.

## 4. Migration without behaviour change

- `PaneLayout`: the existing values, persistence, presets and `combinedMode`/`gridMode` logic in `Main.qml` are unchanged. `onGridModeChanged` only writes when `gridMode` differs from `contentMode === "grid"`, which already holds for compare modes.
- `IPresentationWindow`, `PresentationWindow` and `DisplayManager` are unchanged. Only the two pure conversion functions are added beside them.
- `Main.qml` gains one component instantiation (`CompareView.qml`) in the region the lead grants C3. Everything else lives in C3's component, `src/app/compare_session.cpp` and `tests/compare_panes_ui_tests.cpp`.
- C3 applies the ui-design-skills (application-interface-design, layout, spacing, geometry, motion, design-review) to the compare view, per AGENTS.md.

## 5. Test plan (`tests/compare_panes_tests.cpp`, `tests/compare_panes_ui_tests.cpp`; generated media)

- **Removing a pane never deletes:** remove each pane in 2-up and 4-up with mouse, `Delete` and `Backspace`. Source hashes, mtimes and existence are unchanged, the fake file-operation services record zero calls, and no confirmation opens.
- **Candidate replacement and late completion:** replace pane 2 while its decode is in flight. The old result is dropped (generation), the reservation count returns to its prior value and the new image appears. Same for `swap` and `end` during a decode.
- **Every pane in the ledger:** four distinct images give four CpuPixels charges and textures within the group quota. The same image in pane 0 and as the current image is charged once on the CPU. `end()` returns the ledger to baseline. A 4-up of four 60 MP images gives `TooLarge` per pane with the text, and the current image stays intact.
- **Refusal and pressure:** a lowered effective limit gives `Refused` on the last pane only. Simulated Elevated pressure with the window inactive reclaims inactive-pane textures, and reactivating re-uploads them without a decode.
- **Two windows, same source:** two windows (two processes) compare the same files. Each process accounts on its own and no file is written. The presentation comparison mirrors panes 0 and 1 through the conversion functions (round-trip test).
- **Persistence and compatibility:** compare mode survives neither restart nor `savePreset`. `snapshot()` stores `baseContentMode`. A settings file with `"mode":"compare2"` (hand-edited) loads as `preview`, keeping today's fallback.
- **Linking:** linked zoom/pan/rotate propagate. Independent mode keeps per-pane state. `restoreViewport` round-trips.
- **Keyboard and scope:** 1–4 select a pane, Tab cycles, L toggles the link, Escape exits. Text fields and modals take precedence (IN-02). Large text and 540×360 layouts keep pane labels readable (E6).

## 6. Open questions for the lead

1. **Delete key in compare scope:** bind it to "remove from comparison" (proposed, safest), or keep FS-02 delete-current with confirmation and use another key for removal?
2. **Per-pane texture share before D1:** accept `TooLarge` for over-budget panes in 4-up, or raise the main-window canvas-group quota while compare is active? The latter changes budgets, so it needs E1 evidence.
3. **Should C3 depend on D1**, the fit tier for panes (labelled "Preview quality"), or ship first with `TooLarge` and adopt the tier later?
4. **Compare decode concurrency:** one compare decode besides the current one (proposed), or storage-aware through `IoScheduler::maxConcurrentBackgroundWorkers`?
5. **Session-only compare mode:** confirm that compare is never persisted or restored (proposed), including by F015 tab recovery.

## 7. Non-goals

- No change to `IPresentationWindow`, the presentation window or monitor customization.
- No blink, wipe or difference (C4), no burst stacks (C5) and no rating UI (C2) here.
- No decoding, scaling or file access in `CompareSession` or the surface, and no compare-specific cache.
- No more than four panes, and no compare across windows or processes.
- No persistence of candidates or compare state.
