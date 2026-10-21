#include "presentation_window.hpp"
#include "image_canvas.hpp"
#include "glancelot/display_manager.hpp"
#include <QQuickItem>
#include <QColor>
#include <QPointer>

namespace glancelot {

PresentationWindow::PresentationWindow(QWindow* parent)
    : QQuickWindow(parent) {
    setTitle(QStringLiteral("Glancelot - Presentation"));
    setColor(QColor(0, 0, 0));
    setFlags(Qt::Window | Qt::FramelessWindowHint);

    m_canvasA = new ImageCanvas(contentItem());
    m_canvasA->setTextureBudget(kPresentationTextureBudget / 2);
    m_canvasA->setBackground(QColor(0, 0, 0));
    m_canvasA->fit(m_fitMode);

    m_canvasB = new ImageCanvas(contentItem());
    m_canvasB->setTextureBudget(kPresentationTextureBudget / 2);
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
    if (!m_comparisonMode && m_canvasB) {
        m_canvasB->setImage(QImage());
    }
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
        if (!image.isNull()) {
            m_canvasB->fit(m_fitMode);
        }
    }
}

void PresentationWindow::setFitMode(const QString& mode) {
    if (mode != "shrink" && mode != "fit" && mode != "fill" && mode != "width" && mode != "height") return;
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
    switch (event->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
        QQuickWindow::keyPressEvent(event);
        return;
    default:
        break;
    }

    // Escape is the reserved dismiss key. If in a chord, Escape cancels the chord.
    // If not in a chord, Escape dismisses the window.
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        if (!m_chordPrefix.isEmpty()) {
            m_chordPrefix.clear();
            event->accept();
            return;
        }
        event->accept();
        emit escapePressed();
        return;
    }

    if (m_toggleBinding) {
        const QKeySequence bound = QKeySequence::fromString(m_toggleBinding(), QKeySequence::PortableText);
        if (!bound.isEmpty()) {
            const QKeyCombination currentCombo(event->modifiers(), Qt::Key(event->key()));
            const int step = m_chordPrefix.size();
            if (step < bound.count() && bound[step] == currentCombo) {
                m_chordPrefix.append(currentCombo);
                if (m_chordPrefix.size() == bound.count()) {
                    m_chordPrefix.clear();
                    event->accept();
                    emit escapePressed();
                    return;
                }
                event->accept();
                return;
            }
            m_chordPrefix.clear();
        }
    }

    QQuickWindow::keyPressEvent(event);
}

void PresentationWindow::focusOutEvent(QFocusEvent* event) {
    m_chordPrefix.clear();
    QQuickWindow::focusOutEvent(event);
}

void PresentationWindow::hideEvent(QHideEvent* event) {
    m_chordPrefix.clear();
    QQuickWindow::hideEvent(event);
}

void PresentationWindow::closeEvent(QCloseEvent* event) {
    emit closedByUser();
    event->accept();
}

void PresentationWindow::resizeEvent(QResizeEvent* event) {
    QQuickWindow::resizeEvent(event);
    updateLayout();
}

class DefaultPresentationWindowWrapper : public IPresentationWindow {
public:
    explicit DefaultPresentationWindowWrapper(DisplayManager* owner) {
        m_window = new PresentationWindow();
        if (owner) {
            m_window->setToggleBindingProvider([owner] { return owner->toggleBinding(); });
            QObject::connect(m_window.data(), &PresentationWindow::escapePressed, owner, &DisplayManager::closePresentation);
            QObject::connect(m_window.data(), &PresentationWindow::closedByUser, owner, &DisplayManager::closePresentation);
        }
    }
    void setScreen(QScreen* screen) override { if (m_window) m_window->setScreen(screen); }
    void setGeometry(const QRect& geom) override { if (m_window) m_window->setGeometry(geom); }
    void show() override { if (m_window) m_window->show(); }
    void hide() override { if (m_window) m_window->hide(); }
    void deleteLater() override { if (m_window) { m_window->deleteLater(); m_window = nullptr; } }
    void setComparisonMode(bool comparison) override { if (m_window) m_window->setComparisonMode(comparison); }
    void setPrimaryImage(const QImage& image) override { if (m_window) m_window->setPrimaryImage(image); }
    void setSecondaryImage(const QImage& image) override { if (m_window) m_window->setSecondaryImage(image); }
    void setFitMode(const QString& mode) override { if (m_window) m_window->setFitMode(mode); }
    QQuickWindow* quickWindow() override { return m_window.data(); }

private:
    QPointer<PresentationWindow> m_window;
};

void registerPresentationWindowFactory() {
    DisplayManager::setPresentationWindowFactory([](DisplayManager* owner) -> std::unique_ptr<IPresentationWindow> {
        return std::make_unique<DefaultPresentationWindowWrapper>(owner);
    });
}

namespace {
struct PresentationWindowAutoRegister {
    PresentationWindowAutoRegister() {
        registerPresentationWindowFactory();
    }
};
static PresentationWindowAutoRegister s_autoRegister;
} // namespace

} // namespace glancelot
