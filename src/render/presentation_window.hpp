#pragma once
#include <QQuickWindow>
#include <QImage>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include "image_canvas.hpp"

namespace glancelot {

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

    ImageCanvas* primaryCanvas() const { return m_canvasA; }
    ImageCanvas* secondaryCanvas() const { return m_canvasB; }

signals:
    void escapePressed();
    void closedByUser();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLayout();

    ImageCanvas* m_canvasA{nullptr};
    ImageCanvas* m_canvasB{nullptr};
    bool m_comparisonMode{false};
    QString m_fitMode{"shrink"};
};

} // namespace glancelot

