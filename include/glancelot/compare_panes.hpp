#pragma once
#include "resource_ledger.hpp"
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

// Lead-frozen COMPARE-PANES (docs/contracts/COMPARE-PANES.md). Implemented by packet C3. GUI thread only. Compare never decodes
// and never touches files: it asks the window's ViewerController to decode through the pane decode path and receives values.
namespace glancelot {
inline constexpr int kMaxComparePanes = 4;
// The PaneLayout center content modes. Off is one of the three persisted modes ("preview", "grid", "combined"); the compare
// modes are session-only and PaneLayout accepts them only once C3 applies the contract's diff.
enum class CompareLayout : quint8 { Off, Two, Four };
inline constexpr std::string_view kContentModeCompare2{"compare2"}, kContentModeCompare4{"compare4"};
constexpr std::optional<CompareLayout> parseCompareContentMode(std::string_view mode) {
    if (mode == kContentModeCompare2) return CompareLayout::Two;
    if (mode == kContentModeCompare4) return CompareLayout::Four;
    return std::nullopt;
}
constexpr int comparePaneCount(CompareLayout layout) {
    return layout == CompareLayout::Two ? 2 : layout == CompareLayout::Four ? 4 : 0;
}
enum class PaneState : quint8 { Empty, Loading, Ready, Failed, Refused /*ledger*/, TooLarge /*per-pane GPU*/ };

// The in-app counterpart of IPresentationWindow's comparison surface, implemented by the QML compare host (C3) and by a fake
// in tests. Fit modes, viewport keys and commands use the presentation vocabulary.
class ICompareSurface {
public:
    virtual ~ICompareSurface() = default;
    virtual void setLayout(CompareLayout layout) = 0;
    virtual void setPaneImage(int pane, const QImage& image, quint64 generation) = 0; // a null image clears the pane
    virtual void setActivePane(int pane) = 0;
    virtual void setFitMode(const QString& mode) = 0;
    // {"panes":[ImageCanvas::viewState()...], "linked":bool, "active":int}
    virtual QVariantMap viewportState() const = 0;
    virtual void restoreViewport(const QVariantMap& state) = 0;
    // "actual" | "rotate" | "linked" | "fit:<mode>" | "zoom-in" | "zoom-out" | "pan:<dx>,<dy>"
    virtual void viewportCommand(const QString& command) = 0;
    virtual qint64 admittedTextureBytes() const = 0;            // the window's canvas-group owner view
};

// Two-pane presentation state <-> compare state, so "send comparison to presentation" can reuse both.
QVariantMap compareStateFromPresentation(const QVariantMap& presentation); // primary, secondary, linked, activeSecondary
QVariantMap presentationStateFromCompare(const QVariantMap& compare);      // panes[0..1] only

class CompareSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int paneCount READ paneCount NOTIFY changed)              // 0, 2 or 4
    Q_PROPERTY(int activePane READ activePane WRITE setActivePane NOTIFY changed)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY changed)
    // [{pane, path, name, state, tier, message}]: values only, no QImage in QML
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
public:
    // Reads the controller's current selection generation, for handing a pane to DisplayManager. It never increments it: that
    // would cancel the current image's decode. Pane requests are fenced by the session's own counter.
    using GenerationSource = std::function<quint64()>;
    // `owner` is this window's compare ledger owner, charged for every pane.
    CompareSession(GenerationSource generations, resource::OwnerId owner, QObject* parent = nullptr);
    ~CompareSession() override;
    void setSurface(ICompareSurface* surface);   // not owned; null detaches. deliver, swap and removal drive it
    int paneCount() const; int activePane() const; bool linked() const; QVariantList candidates() const;
    void setActivePane(int pane); void setLinked(bool linked);

    Q_INVOKABLE bool begin(int panes, const QStringList& paths);   // 2 or 4; paths.size() <= panes; files only
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
