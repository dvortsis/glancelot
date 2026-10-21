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

#include <functional>

class ImageCanvas;

namespace glancelot {

class DisplayManager;

// Aggregate GPU texture admission limit for one presentation window (split across its canvases).
constexpr qint64 kPresentationTextureBudget = 256LL * 1024 * 1024;

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

    ImageCanvas* primaryCanvas() const { return m_canvasA; }
    ImageCanvas* secondaryCanvas() const { return m_canvasB; }

signals:
    void escapePressed();
    void closedByUser();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void updateLayout();

    ImageCanvas* m_canvasA{nullptr};
    ImageCanvas* m_canvasB{nullptr};
    bool m_comparisonMode{false};
    QString m_fitMode{"shrink"};
    std::function<QString()> m_toggleBinding;
    QList<QKeyCombination> m_chordPrefix;
};

void registerPresentationWindowFactory();

} // namespace glancelot

