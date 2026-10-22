#pragma once
// Test-only process-local topology. Never use on the Windows/XCB platform plugins.
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <QGuiApplication>
#include <QScreen>
#include <QCoreApplication>
#include <QEvent>
#include <vector>
#include <algorithm>
class MonitorSyntheticScreen final:public QPlatformScreen {
    QRect area_;QString serial_;qreal scale_;
public:
    MonitorSyntheticScreen(QRect area,QString serial,qreal scale):area_(area),serial_(std::move(serial)),scale_(scale){}
    QRect geometry() const override{return area_;}
    QRect availableGeometry() const override{return area_.adjusted(0,0,0,-40);}
    int depth() const override{return 32;}
    QImage::Format format() const override{return QImage::Format_RGB32;}
    QSizeF physicalSize() const override{return {600,340};}
    QDpi logicalDpi() const override{return {96,96};}
    qreal devicePixelRatio() const override{return scale_;}
    QString name() const override{return "Generated test display";}
    QString manufacturer() const override{return "Generated";}
    QString model() const override{return "Topology fixture";}
    QString serialNumber() const override{return serial_;}
    QList<QPlatformScreen*> virtualSiblings() const override{QList<QPlatformScreen*> result;for(auto* screen:QGuiApplication::screens())result.append(screen->handle());return result;}
};
class MonitorSyntheticScreens final {
    std::vector<MonitorSyntheticScreen*> screens_;
public:
    ~MonitorSyntheticScreens(){QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);while(!screens_.empty())remove(screens_.back());}
    MonitorSyntheticScreen* add(QRect area,QString serial,qreal dpr=1){Q_ASSERT(QGuiApplication::platformName()=="offscreen");auto* screen=new MonitorSyntheticScreen(area,std::move(serial),dpr);screens_.push_back(screen);QWindowSystemInterface::handleScreenAdded(screen);return screen;}
    void remove(MonitorSyntheticScreen* screen){const auto it=std::find(screens_.begin(),screens_.end(),screen);if(it==screens_.end())return;screens_.erase(it);QWindowSystemInterface::handleScreenRemoved(screen);}
};
