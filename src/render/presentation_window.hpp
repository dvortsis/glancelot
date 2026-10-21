#pragma once
#include <QQuickWindow>
#include <QImage>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QList>
#include <QKeyCombination>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include "output_color.hpp"

#include <functional>

class ImageCanvas;

namespace glancelot {

class DisplayManager;

// Aggregate GPU texture admission limit for one presentation window (split across its canvases).
constexpr qint64 kPresentationTextureBudget = 256LL * 1024 * 1024;
// Conservative reservation for shared CPU atlas, two GPU atlases and in-flight uploads.
// Separately reported from retained decoded pixels; no full-size converted rasters.
constexpr qint64 kPresentationColorReservation = 4LL * 1024 * 1024;

class PresentationWindow : public QQuickWindow {
    Q_OBJECT
public:
    explicit PresentationWindow(QWindow* parent = nullptr);
    ~PresentationWindow() override;

    void setComparisonMode(bool comparison);
    bool isComparisonMode() const { return m_comparisonMode; }

    void setPrimaryImage(const QImage& image);
    void setSecondaryImage(const QImage& image);
    void setFitMode(const QString& mode);
    QString fitMode() const { return m_fitMode; }
    void setToggleBindingProvider(std::function<QString()> provider) { m_toggleBinding = std::move(provider); }
    void setCommandBindingProvider(std::function<QString(const QString&)> provider) { m_commandBinding=std::move(provider); }
    QVariantMap viewportState() const;
    void restoreViewport(const QVariantMap& state);
    void viewportCommand(const QString& command);
    void setOutputColorProfile(const QColorSpace& profile);
    void refreshOutputColorProfile();
    QString outputColorStatus() const { return m_colorStatus; }
    QImage outputColorAtlas() const { return m_colorAtlas; }
    qint64 colorResourceBytes() const { return kPresentationColorReservation; }

    ImageCanvas* primaryCanvas() const { return m_canvasA; }
    ImageCanvas* secondaryCanvas() const { return m_canvasB; }

signals:
    void escapePressed();
    void closedByUser();
    void outputColorChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateLayout();
    void requestColor();
    void zoomOutput(qreal factor, const QPointF& at);
    void panOutput(const QPointF& delta, bool secondary=false);

    ImageCanvas* m_canvasA{nullptr};
    ImageCanvas* m_canvasB{nullptr};
    bool m_comparisonMode{false};
    QString m_fitMode{"shrink"};
    std::function<QString()> m_toggleBinding;
    QList<QKeyCombination> m_chordPrefix;
    QKeySequence m_chordBinding;
    QString m_chordCommand;
    std::function<QString(const QString&)> m_commandBinding;
    QElapsedTimer m_chordElapsed;
    bool m_linked{true},m_dragging{},m_dragSecondary{},m_activeSecondary{};
    QPointF m_lastPointer;
    QVariantMap m_pendingPrimary,m_pendingSecondary;
    QImage m_colorAtlas;
    QString m_colorStatus{QStringLiteral("sRGB fallback (monitor profile pending)")};
    QColorSpace m_injectedProfile;
    bool m_hasInjectedProfile{},m_colorInFlight{};
    quint64 m_colorRevision{};
    QFutureWatcher<output_color::Result> m_colorWatcher;
    QTimer m_colorRetry,m_colorRefresh;
};

void registerPresentationWindowFactory();

} // namespace glancelot

