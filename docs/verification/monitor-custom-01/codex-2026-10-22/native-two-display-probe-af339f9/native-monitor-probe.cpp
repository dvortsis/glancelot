#include <QGuiApplication>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QScreen>
#include <QtTest/QTest>
#include "glancelot/display_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/monitor_customization.hpp"
#include "presentation_window.hpp"
QJsonObject rect(QRect r){return {{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()}};}
int main(int argc,char**argv){QGuiApplication app(argc,argv);QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());glancelot::registerPresentationWindowFactory();glancelot::DisplayManager manager;PaneLayout panes;panes.setSlot(1);glancelot::MonitorCustomization custom(&manager,&panes);custom.setSlot(1);QQuickWindow controller;controller.resize(600,400);controller.show();custom.setControllerWindow(&controller);auto infos=manager.displays();QJsonObject result{{"displayCount",infos.size()}};bool ok=infos.size()>=2;
if(ok){const auto first=infos[0],second=infos[1];QImage image(800,600,QImage::Format_RGB32);image.fill(Qt::red);manager.updateImage("generated-native-probe.png",image,1);ok=custom.setOutputOptions({{"displayId",second.id},{"windowMode","windowed"}})&&manager.openPresentation(second.id);QTest::qWait(600);auto* window=qobject_cast<glancelot::PresentationWindow*>(manager.presentationWindow());ok=ok&&window;
if(window){result["initialFrame"]=rect(window->frameGeometry());result["initialWorkArea"]=rect(second.availableGeometry);result["initialFrameFits"]=second.availableGeometry.contains(window->frameGeometry());ok=ok&&result["initialFrameFits"].toBool();window->showMaximized();QTest::qWait(400);ok=custom.setOutputOptions({{"guides",true}})&&ok;QTest::qWait(150);result["appearanceRetainsMaximized"]=window->visibility()==QWindow::Maximized;ok=ok&&result["appearanceRetainsMaximized"].toBool();const bool moved=custom.moveOutput(first.id);QTest::qWait(600);result["maximizedMoveAccepted"]=moved;result["maximizedMoveActiveId"]=manager.activeDisplayId();result["maximizedMoveVisibility"]=int(window->visibility());window->showNormal();QTest::qWait(400);result["restoredAfterMaxMoveFrame"]=rect(window->frameGeometry());result["firstWorkArea"]=rect(first.availableGeometry);result["restoredAfterMaxMoveFits"]=first.availableGeometry.contains(window->frameGeometry());ok=ok&&moved&&manager.activeDisplayId()==first.id&&result["restoredAfterMaxMoveFits"].toBool();window->showMinimized();QTest::qWait(200);const bool minMoved=custom.moveOutput(second.id);QTest::qWait(500);window->showNormal();QTest::qWait(400);result["minimizedMoveAccepted"]=minMoved;result["restoredAfterMinMoveFrame"]=rect(window->frameGeometry());result["restoredAfterMinMoveFits"]=second.availableGeometry.contains(window->frameGeometry());ok=ok&&minMoved&&manager.activeDisplayId()==second.id&&result["restoredAfterMinMoveFits"].toBool();manager.closePresentation();}}
result["passed"]=ok;QFile out(QString::fromLocal8Bit(argv[1]));out.open(QIODevice::WriteOnly);out.write(QJsonDocument(result).toJson());return ok?0:1;}
