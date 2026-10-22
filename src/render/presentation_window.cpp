#include "presentation_window.hpp"
#include "image_canvas.hpp"
#include "glancelot/display_manager.hpp"
#include <QQuickItem>
#include <QColor>
#include <QPointer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QScreen>
#include <cmath>
#include <QQuickPaintedItem>
#include <QPainter>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QScopedValueRollback>

namespace glancelot {
namespace {
constexpr qint64 informationReservation=1024*1024;
class PresentationInformation final:public QQuickPaintedItem {
    ImageCanvas* canvas;
    bool requested{};
    void scaleTexture() {
        const auto scale=window()?window()->effectiveDevicePixelRatio():qreal(1);
        const bool valid=std::isfinite(scale)&&scale>0&&scale<=64;
        if(valid)setTextureSize({std::max(1,int(std::floor(512/scale))),std::max(1,int(std::floor(96/scale)))});
        setVisible(requested&&valid);
    }
protected:
    void itemChange(ItemChange change,const ItemChangeData& data) override {
        QQuickPaintedItem::itemChange(change,data);
        if(change==ItemSceneChange||change==ItemDevicePixelRatioHasChanged)scaleTexture();
    }
public:
    PresentationInformation(ImageCanvas* value,QQuickItem* parent):QQuickPaintedItem(parent),canvas(value){setSize({320,48});scaleTexture();setZ(20);connect(canvas,&ImageCanvas::viewChanged,this,[this]{update();});connect(canvas,&ImageCanvas::imageChanged,this,[this]{update();});}
    void setRequestedVisible(bool value){requested=value;scaleTexture();}
    void paint(QPainter* painter) override {
        painter->fillRect(boundingRect(),QColor(0,0,0,190));painter->setPen(Qt::white);
        const auto image=canvas->image();painter->drawText(boundingRect().adjusted(8,4,-8,-4),Qt::AlignVCenter,
            QString("%1 × %2  ·  %3%  ·  %4°").arg(image.width()).arg(image.height()).arg(canvas->zoom()*100,0,'f',0).arg(canvas->rotation()));
    }
};
class PresentationGuides final:public QQuickItem {
public:
    explicit PresentationGuides(QQuickItem* parent):QQuickItem(parent){setFlag(ItemHasContents);setZ(19);}
    QSGNode* updatePaintNode(QSGNode* old,UpdatePaintNodeData*) override {
        delete old;auto* node=new QSGGeometryNode;auto* geometry=new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),8);
        geometry->setDrawingMode(QSGGeometry::DrawLines);auto* points=geometry->vertexDataAsPoint2D();
        const float w=float(width()),h=float(height());
        points[0].set(w/3,0);points[1].set(w/3,h);points[2].set(2*w/3,0);points[3].set(2*w/3,h);
        points[4].set(0,h/3);points[5].set(w,h/3);points[6].set(0,2*h/3);points[7].set(w,2*h/3);
        node->setGeometry(geometry);node->setFlag(QSGNode::OwnsGeometry);auto* material=new QSGFlatColorMaterial;material->setColor(QColor(255,255,255,150));node->setMaterial(material);node->setFlag(QSGNode::OwnsMaterial);return node;
    }
};
void copyViewport(ImageCanvas* from,ImageCanvas* to) {
    if (from->image().isNull() || to->image().isNull()) return;
    to->setViewState(from->viewState());
}
}

PresentationWindow::PresentationWindow(QWindow* parent)
    : QQuickWindow(parent) {
    setTitle(QStringLiteral("Glancelot - Presentation"));
    setColor(QColor(0, 0, 0));
    setFlags(Qt::Window | Qt::FramelessWindowHint);

    m_canvasA = new ImageCanvas(contentItem());
    m_canvasA->setTextureBudget(kPresentationTextureBudget);
    m_canvasA->setBackground(QColor(0, 0, 0));
    m_canvasA->fit(m_fitMode);

    m_canvasB = new ImageCanvas(contentItem());
    m_canvasB->shareTextureAdmissionWith(m_canvasA);
    m_canvasB->setTextureBudget(0);
    m_canvasB->setBackground(QColor(0, 0, 0));
    m_canvasB->fit(m_fitMode);
    m_canvasB->setVisible(false);
    m_guides=new PresentationGuides(contentItem());m_guides->setVisible(false);
    m_information=new PresentationInformation(m_canvasA,contentItem());m_information->setPosition({12,12});m_information->setVisible(false);
    m_cursorTimer.setSingleShot(true);m_cursorTimer.setInterval(2000);
    connect(&m_cursorTimer,&QTimer::timeout,this,[this]{if(m_customOptions.value("cursorPolicy")=="auto")setCursor(Qt::BlankCursor);});
    connect(m_canvasA,&ImageCanvas::viewChanged,this,[this]{if(m_userViewport)emit primaryViewportChanged(m_canvasA->viewState());});

    updateLayout();
    m_colorRetry.setSingleShot(true); m_colorRetry.setInterval(50);
    connect(&m_colorRetry,&QTimer::timeout,this,&PresentationWindow::requestColor);
    m_colorRefresh.setInterval(5000);
    connect(&m_colorRefresh,&QTimer::timeout,this,[this] { if (isVisible() && !m_hasInjectedProfile && !m_colorInFlight) { ++m_colorRevision; requestColor(); } });
    m_colorRefresh.start();
    connect(this,&QWindow::screenChanged,this,[this] { refreshOutputColorProfile(); updateLayout(); });
    connect(&m_colorWatcher,&QFutureWatcher<output_color::Result>::finished,this,[this] {
        m_colorInFlight=false; m_colorRetry.stop();
        const auto result=m_colorWatcher.result();
        if (result.revision!=m_colorRevision) { requestColor(); return; }
        if (m_colorAtlas==result.atlas && m_colorStatus==result.status) return;
        m_colorAtlas=result.atlas; m_colorStatus=result.status;
        m_canvasA->setOutputColorLut(m_colorAtlas); m_canvasB->setOutputColorLut(m_colorAtlas);
        emit outputColorChanged();
    });
    refreshOutputColorProfile();
}

PresentationWindow::~PresentationWindow() = default;
void PresentationWindow::applyControllerViewport(const QVariantMap& state) {
    m_canvasA->setViewState(state);if(m_linked)copyViewport(m_canvasA,m_canvasB);
}

void PresentationWindow::applyCustomization(const QVariantMap& options) {
    const auto previous=m_customOptions;m_customOptions=options;
    if(previous.value("fitMode")!=options.value("fitMode"))setFitMode(options.value("fitMode","shrink").toString());
    const QColor background(options.value("backgroundColor","#000000").toString());
    m_canvasA->setBackground(background);m_canvasB->setBackground(background);setColor(background);
    const bool checker=options.value("background")=="checker";
    m_canvasA->setCheckerboard(checker);m_canvasB->setCheckerboard(checker);
    m_guides->setVisible(options.value("guides").toBool());static_cast<PresentationInformation*>(m_information)->setRequestedVisible(options.value("imageInfo").toBool());
    setComparisonMode(m_comparisonMode);
    const auto cursor=options.value("cursorPolicy").toString();m_cursorTimer.stop();
    setCursor(cursor=="hidden"?Qt::BlankCursor:Qt::ArrowCursor);if(cursor=="auto")m_cursorTimer.start();
    if(previous.value("windowMode")!=options.value("windowMode"))setFlags(options.value("windowMode")=="windowed"?Qt::WindowFlags(Qt::Window):Qt::WindowFlags(Qt::Window|Qt::FramelessWindowHint));
    if(options.value("windowMode")=="windowed"&&visibility()!=QWindow::Windowed)showNormal();
    else if(options.value("windowMode")=="fullscreen"&&visibility()!=QWindow::FullScreen)showFullScreen();
    updateLayout();
}

void PresentationWindow::setComparisonMode(bool comparison) {
    const auto budget=kPresentationTextureBudget-(m_customOptions.value("imageInfo").toBool()?informationReservation:0);
    m_canvasA->setAggregateTextureBudget(budget);m_canvasB->setAggregateTextureBudget(budget);
    m_canvasA->setTextureBudget(comparison?budget/2:budget);
    m_canvasB->setTextureBudget(comparison?budget/2:0);
    if (m_comparisonMode == comparison) return;
    m_comparisonMode = comparison;
    if (!comparison) m_activeSecondary=false;
    m_canvasB->setVisible(m_comparisonMode);
    if (!m_comparisonMode && m_canvasB) {
        setSecondaryImage(QImage());
    }
    updateLayout();
}

void PresentationWindow::setPrimaryImage(const QImage& image) {
    if (m_canvasA) {
        const auto previous=m_canvasA->image();
        const auto state=previous.isNull()?m_pendingPrimary:m_canvasA->viewState();
        if (!previous.isNull()) m_pendingPrimary=state;
        m_canvasA->setImage(image);
        if (!image.isNull()) {
            if(!state.isEmpty()) m_canvasA->setViewState(state);
            else if(m_fitMode=="actual") m_canvasA->actualSize(); else m_canvasA->fit(m_fitMode);
        }
    }
}

void PresentationWindow::setSecondaryImage(const QImage& image) {
    if (m_canvasB) {
        const auto previous=m_canvasB->image();
        const auto state=previous.isNull()?m_pendingSecondary:m_canvasB->viewState();
        if (!previous.isNull()) m_pendingSecondary=state;
        m_canvasB->setImage(image);
        if (!image.isNull()) {
            if (!state.isEmpty()) m_canvasB->setViewState(state);
            else if(m_fitMode=="actual") m_canvasB->actualSize(); else m_canvasB->fit(m_fitMode);
            if (m_linked) copyViewport(m_canvasA,m_canvasB);
        }
    }
}

void PresentationWindow::setFitMode(const QString& mode) {
    if (mode != "shrink" && mode != "fit" && mode != "fill" && mode != "width" && mode != "height" && mode!="actual") return;
    m_fitMode = mode;
    m_pendingPrimary={{"fit",mode}}; m_pendingSecondary={{"fit",mode}};
    if (mode=="actual") { m_canvasA->actualSize(); m_canvasB->actualSize(); }
    else { if (m_canvasA) m_canvasA->fit(mode); if (m_canvasB) m_canvasB->fit(mode); }
}

QVariantMap PresentationWindow::viewportState() const {
    return {{"primary",m_canvasA->image().isNull()?m_pendingPrimary:m_canvasA->viewState()},{"secondary",m_canvasB->image().isNull()?m_pendingSecondary:m_canvasB->viewState()},{"linked",m_linked},{"activeSecondary",m_activeSecondary}};
}
void PresentationWindow::restoreViewport(const QVariantMap& state) {
    if (state.isEmpty()) { m_linked=true; m_activeSecondary=false; setFitMode("shrink"); return; }
    m_pendingPrimary=state.value("primary").toMap(); m_pendingSecondary=state.value("secondary").toMap();
    if(!m_canvasA->image().isNull() || !m_pendingPrimary.value("fit").toString().isEmpty()) m_canvasA->setViewState(m_pendingPrimary);
    if(!m_canvasB->image().isNull() || !m_pendingSecondary.value("fit").toString().isEmpty()) m_canvasB->setViewState(m_pendingSecondary);
    m_linked=state.value("linked",true).toBool(); m_fitMode=m_pendingPrimary.value("fit",m_canvasA->fitMode()).toString();
    m_activeSecondary=m_comparisonMode && state.value("activeSecondary",false).toBool();
}
void PresentationWindow::zoomOutput(qreal factor,const QPointF& at) {
    const bool secondary=m_comparisonMode && at.x()>=m_canvasB->x();
    m_activeSecondary=secondary;
    auto* target=secondary?m_canvasB:m_canvasA;
    const auto local=at-QPointF(target->x(),target->y());
    target->zoomAt(factor,local.x(),local.y());
    if (m_comparisonMode && m_linked) {
        auto* other=secondary?m_canvasA:m_canvasB;
        copyViewport(target,other);
    }
    m_fitMode=target->fitMode();
}
void PresentationWindow::panOutput(const QPointF& delta,bool secondary) {
    m_activeSecondary=m_comparisonMode && secondary;
    auto* target=secondary?m_canvasB:m_canvasA;
    target->panBy(delta.x(),delta.y());
    if (m_comparisonMode && m_linked) {
        auto* other=secondary?m_canvasA:m_canvasB;
        copyViewport(target,other);
    }
    m_fitMode=target->fitMode();
}
void PresentationWindow::viewportCommand(const QString& command) {
    QScopedValueRollback<bool> gesture(m_userViewport,true);
    if (command=="reference") { if(m_comparisonMode) m_activeSecondary=!m_activeSecondary; return; }
    auto* active=m_activeSecondary?m_canvasB:m_canvasA;
    auto* other=m_activeSecondary?m_canvasA:m_canvasB;
    if (command=="actual") { if(m_linked) setFitMode("actual"); else active->actualSize(); return; }
    if (command=="rotate") { active->rotateClockwise();if(m_linked&&m_comparisonMode)copyViewport(active,other);return; }
    if (command=="linked") { m_linked=!m_linked; if(m_linked && m_comparisonMode) copyViewport(active,other); return; }
    if (command=="fit") {
        const QStringList modes{"shrink","fit","fill"}; const auto mode=modes[(modes.indexOf(active->fitMode())+1)%modes.size()];
        if(m_linked) setFitMode(mode); else active->fit(mode); return;
    }
    if (command=="zoom-in" || command=="zoom-out") zoomOutput(command=="zoom-in"?1.25:.8,QPointF(active->x()+active->width()/2,height()/2));
    else if (command=="pan-left") panOutput({40,0},m_activeSecondary);
    else if (command=="pan-right") panOutput({-40,0},m_activeSecondary);
    else if (command=="pan-up") panOutput({0,40},m_activeSecondary);
    else if (command=="pan-down") panOutput({0,-40},m_activeSecondary);
}
void PresentationWindow::setOutputColorProfile(const QColorSpace& profile) {
    m_hasInjectedProfile=true;
    m_injectedProfile=profile.isValid() && profile.iccProfile().size()<=output_color::maximumProfileBytes ? profile : QColorSpace{};
    refreshOutputColorProfile();
}
void PresentationWindow::refreshOutputColorProfile() {
    ++m_colorRevision;
    m_colorAtlas={}; m_colorStatus=QStringLiteral("sRGB fallback (monitor profile pending)");
    m_canvasA->setOutputColorLut({}); m_canvasB->setOutputColorLut({});
    emit outputColorChanged();
    requestColor();
}
void PresentationWindow::requestColor() {
    if (m_colorInFlight) { m_colorRetry.start(); return; }
    bool expected=false;
    if (!output_color::workerBusy.compare_exchange_strong(expected,true)) { m_colorRetry.start(); return; }
    m_colorRetry.stop(); m_colorInFlight=true;
    const auto name=screen()?screen()->name():QString();
    const auto profile=m_injectedProfile; const bool injected=m_hasInjectedProfile; const auto revision=m_colorRevision;
    m_colorWatcher.setFuture(QtConcurrent::run([name,profile,injected,revision] { return output_color::build(name,profile,injected,revision); }));
}

void PresentationWindow::updateLayout() {
    if(m_guides)m_guides->setSize(QSizeF(width(),height()));
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

    if (event->isAutoRepeat()) { event->accept(); return; }
    const auto bindingFor=[this](const QString& id) {
        return QKeySequence::fromString(id=="presentation-toggle" ? (m_toggleBinding?m_toggleBinding():QString()) : (m_commandBinding?m_commandBinding(id):QString()),QKeySequence::PortableText);
    };
    if (!m_chordPrefix.isEmpty() && (!m_chordElapsed.isValid() || m_chordElapsed.elapsed()>=1500 || bindingFor(m_chordCommand)!=m_chordBinding)) m_chordPrefix.clear();
    if (event->key()==Qt::Key_Escape && event->modifiers()==Qt::NoModifier) {
        event->accept();
        if (!m_chordPrefix.isEmpty()) { m_chordPrefix.clear(); return; }
        emit escapePressed(); return;
    }
    const QKeyCombination combo(event->modifiers(),Qt::Key(event->key()));
    QStringList ids{"presentation-toggle","presentation-fit","monitor-recover","monitor-disable","monitor-settings"};
    for (const auto* suffix : {"rotate","actual","zoom-in","zoom-out","pan-left","pan-right","pan-up","pan-down","linked","reference"}) ids<<QStringLiteral("presentation-view-")+QLatin1String(suffix);
    if (!m_chordPrefix.isEmpty()) ids={m_chordCommand};
    for (const auto& id:ids) {
        const auto bound=bindingFor(id);
        const int step=m_chordPrefix.size();
        if (bound.isEmpty() || step>=bound.count() || bound[step]!=combo) continue;
        if (!step) { m_chordBinding=bound; m_chordCommand=id; m_chordElapsed.start(); }
        m_chordPrefix.append(combo); event->accept();
        if (m_chordPrefix.size()==bound.count()) {
            m_chordPrefix.clear();
            if (id=="presentation-toggle") emit escapePressed();
            else if(id.startsWith("monitor-"))emit customizationCommand(id);
            else viewportCommand(id=="presentation-fit"?QStringLiteral("fit"):id.mid(18));
        }
        return;
    }
    m_chordPrefix.clear();
    QQuickWindow::keyPressEvent(event);
}

void PresentationWindow::focusOutEvent(QFocusEvent* event) {
    m_chordPrefix.clear();
    m_dragging=false;
    QQuickWindow::focusOutEvent(event);
}

void PresentationWindow::hideEvent(QHideEvent* event) {
    m_chordPrefix.clear();
    m_dragging=false;
    QQuickWindow::hideEvent(event);
}
void PresentationWindow::wheelEvent(QWheelEvent* event) {
    QScopedValueRollback<bool> gesture(m_userViewport,true);
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal steps=!event->pixelDelta().isNull()?event->pixelDelta().y()/120.0:event->angleDelta().y()/120.0;
        zoomOutput(std::pow(1.2,qBound(qreal(-20),steps,qreal(20))),event->position());
    } else {
        const QPointF delta=!event->pixelDelta().isNull()?QPointF(event->pixelDelta()):QPointF(event->angleDelta())/3;
        panOutput(delta,m_comparisonMode && event->position().x()>=m_canvasB->x());
    }
    event->accept();
}
void PresentationWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton && event->button()!=Qt::MiddleButton) { QQuickWindow::mousePressEvent(event); return; }
    m_dragging=true; m_dragSecondary=m_comparisonMode && event->position().x()>=m_canvasB->x();
    m_activeSecondary=m_dragSecondary;
    m_lastPointer=event->position(); event->accept();
}
void PresentationWindow::mouseMoveEvent(QMouseEvent* event) {
    QScopedValueRollback<bool> gesture(m_userViewport,true);
    const auto cursor=m_customOptions.value("cursorPolicy").toString();
    if(cursor=="auto"){setCursor(Qt::ArrowCursor);m_cursorTimer.start();}
    if (!m_dragging) { QQuickWindow::mouseMoveEvent(event); return; }
    panOutput(event->position()-m_lastPointer,m_dragSecondary); m_lastPointer=event->position(); event->accept();
}
void PresentationWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging) { QQuickWindow::mouseReleaseEvent(event); return; }
    m_dragging=false; event->accept();
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
            m_window->setCommandBindingProvider([owner](const QString& id) { return owner->outputBinding(id); });
            QObject::connect(m_window.data(), &PresentationWindow::escapePressed, owner, &DisplayManager::closePresentation);
            QObject::connect(m_window.data(), &PresentationWindow::closedByUser, owner, &DisplayManager::closePresentation);
            QObject::connect(m_window.data(), &PresentationWindow::outputColorChanged, owner, &DisplayManager::outputColorStatusChanged);
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
    QVariantMap viewportState() const override { return m_window ? m_window->viewportState() : QVariantMap{}; }
    void restoreViewport(const QVariantMap& state) override { if (m_window) m_window->restoreViewport(state); }
    void viewportCommand(const QString& command) override { if (m_window) m_window->viewportCommand(command); }
    QString outputColorStatus() const override { return m_window?m_window->outputColorStatus():QStringLiteral("Presentation closed"); }
    qint64 colorResourceBytes() const override { return m_window?m_window->colorResourceBytes():0; }
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
