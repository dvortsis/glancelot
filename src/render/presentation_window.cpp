#include "presentation_window.hpp"
#include <QQuickItem>
#include <QColor>

namespace glancelot {

PresentationWindow::PresentationWindow(QWindow* parent)
    : QQuickWindow(parent) {
    setTitle(QStringLiteral("Glancelot - Presentation"));
    setColor(QColor(0, 0, 0));
    setFlags(Qt::Window | Qt::FramelessWindowHint);

    m_canvasA = new ImageCanvas(contentItem());
    m_canvasA->setBackground(QColor(0, 0, 0));
    m_canvasA->fit(m_fitMode);

    m_canvasB = new ImageCanvas(contentItem());
    m_canvasB->setBackground(QColor(0, 0, 0));
    m_canvasB->fit(m_fitMode);
    m_canvasB->setVisible(false);

    updateLayout();
}

PresentationWindow::~PresentationWindow() = default;

void PresentationWindow::setComparisonMode(bool comparison) {
    if (m_comparisonMode == comparison) return;
    m_comparisonMode = comparison;
    m_canvasB->setVisible(m_comparisonMode);
    updateLayout();
}

void PresentationWindow::setPrimaryImage(const QImage& image) {
    if (m_canvasA) {
        m_canvasA->setImage(image);
        m_canvasA->fit(m_fitMode);
    }
}

void PresentationWindow::setSecondaryImage(const QImage& image) {
    if (m_canvasB) {
        m_canvasB->setImage(image);
        m_canvasB->fit(m_fitMode);
    }
}

void PresentationWindow::setFitMode(const QString& mode) {
    m_fitMode = mode;
    if (m_canvasA) m_canvasA->fit(mode);
    if (m_canvasB) m_canvasB->fit(mode);
}

void PresentationWindow::updateLayout() {
    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0) return;

    if (!m_comparisonMode) {
        if (m_canvasA) {
            m_canvasA->setX(0);
            m_canvasA->setY(0);
            m_canvasA->setSize(QSizeF(w, h));
        }
    } else {
        // Split side-by-side (left/right)
        const qreal halfW = w / 2.0;
        if (m_canvasA) {
            m_canvasA->setX(0);
            m_canvasA->setY(0);
            m_canvasA->setSize(QSizeF(halfW, h));
        }
        if (m_canvasB) {
            m_canvasB->setX(halfW);
            m_canvasB->setY(0);
            m_canvasB->setSize(QSizeF(halfW, h));
        }
    }
}

void PresentationWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F5) {
        event->accept();
        emit escapePressed();
        close();
        return;
    }
    QQuickWindow::keyPressEvent(event);
}

void PresentationWindow::closeEvent(QCloseEvent* event) {
    emit closedByUser();
    event->accept();
}

void PresentationWindow::resizeEvent(QResizeEvent* event) {
    QQuickWindow::resizeEvent(event);
    updateLayout();
}

} // namespace glancelot
