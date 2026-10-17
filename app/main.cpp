#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/window_slot.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "thumbnail_provider.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>
#include <QIcon>
#include <memory>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    QCoreApplication::setApplicationName("Glancelot");
    QCoreApplication::setOrganizationName("Glancelot");
    QCoreApplication::setApplicationVersion("0.1.0-alpha.1");
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.setApplicationDescription("A local-first native photo viewer. JPEG/PNG alpha.");
    parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument("image","Image to open.","[image]");
    parser.addOption({"quit-after","Exit after the given milliseconds (test automation).","milliseconds"});
    parser.addOption({"screenshot","Save a canvas/window screenshot before timed exit.","path"});
    parser.addOption({"report","Write local JSON startup/render evidence before timed exit.","path"});
    parser.addOption({"theme","Select a built-in family (saved preference).","id"});
    parser.addOption({"theme-mode","Select system, dark, or light (saved preference).","mode"});
    parser.addOption({"show-settings","Open settings at startup (test automation)."});
    parser.addOption({"show-themes","Open the theme picker at startup (test automation)."});
    parser.addOption({"reset-settings","Set a damaged or incompatible settings.json aside (renamed, never deleted) and start with defaults."});
    parser.addOption({"no-resume","Do not claim a window slot or remember/restore the browsing position (test automation)."});
    parser.addOption({"window-size","Initial logical window size for UI verification, e.g. 540x360.","size"});
    parser.process(app);
    QElapsedTimer elapsed;elapsed.start();
    if(parser.isSet("reset-settings")){QString moved;if(glancelot::resetSettings(&moved)&&!moved.isEmpty())std::fprintf(stderr,"Glancelot: settings set aside as %s\n",qPrintable(moved));}
    // The slot is claimed before the window shows; it is released on normal exit, and a crashed window's slot is
    // reclaimed by the next claim (pid plus process start time).
    std::unique_ptr<WindowSlot> windowSlot;
    if(!parser.isSet("no-resume"))windowSlot=std::make_unique<WindowSlot>(QString{},0);
    // Keep ownership until after the controller destructor has finished its final save.
    ViewerController viewer;
    viewer.setWindowSlot(windowSlot?windowSlot->number():0);
    QObject::connect(&app,&QGuiApplication::aboutToQuit,&app,[&]{viewer.flushWindowPosition();});
    // If coordination was busy at startup, keep trying for a while instead of silently running without memory.
    QTimer slotRetry;slotRetry.setInterval(4000);int slotAttempts=0;
    if(windowSlot&&windowSlot->number()==0){
        QObject::connect(&slotRetry,&QTimer::timeout,&app,[&]{if(windowSlot->claim(0)){viewer.setWindowSlot(windowSlot->number());viewer.restoreWindow();slotRetry.stop();}else if(++slotAttempts>=8)slotRetry.stop();});
        slotRetry.start();
    }
    ThemeManager theme;
    if(parser.isSet("theme"))theme.setFamily(parser.value("theme"));
    if(parser.isSet("theme-mode"))theme.setMode(parser.value("theme-mode"));
    app.setWindowIcon(QIcon(":/brand/icons/app/dark/glancelot-256.png"));
    qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");
    // Declared before the engine so image-provider consumers are destroyed before the service and its workers.
    QString decoderProgram=QCoreApplication::applicationDirPath()+"/glancelot-decoder";
#ifdef Q_OS_WIN
    decoderProgram+=".exe";
#endif
    ThumbnailService thumbnails(decoderProgram);
    thumbnails.setWorkerCount(2);
    FilmstripModel filmstrip;
    QObject::connect(&viewer,&ViewerController::stateChanged,&filmstrip,[&]{filmstrip.sync(viewer.snapshot());});
    QObject::connect(&viewer,&ViewerController::filesInvalidated,&filmstrip,[&]{filmstrip.invalidate();});
    // The tree only follows navigation while it is wanted, so a hidden tree never lists folders.
    FolderTreeModel folderTree;
    QObject::connect(&viewer,&ViewerController::stateChanged,&folderTree,[&]{if(viewer.treeVisible())folderTree.sync(viewer.snapshot());});
    QObject::connect(&filmstrip,&FilmstripModel::epochChanged,&thumbnails,[&]{thumbnails.setEpoch(filmstrip.epoch());});
    QQmlApplicationEngine engine;
    engine.addImageProvider("thumb",new ThumbnailProvider(&thumbnails));
    engine.rootContext()->setContextProperty("viewer",&viewer);
    engine.rootContext()->setContextProperty("filmstrip",&filmstrip);
    engine.rootContext()->setContextProperty("folderTree",&folderTree);
    engine.rootContext()->setContextProperty("theme",&theme);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(2);},Qt::QueuedConnection);
    engine.loadFromModule("Glancelot","Main");
    if(engine.rootObjects().isEmpty())return 2;
    if(parser.isSet("show-themes"))QTimer::singleShot(100,engine.rootObjects().first(),[&]{QMetaObject::invokeMethod(engine.rootObjects().first(),"openThemes");});
    if(parser.isSet("show-settings"))QTimer::singleShot(100,&viewer,[&]{viewer.trigger("settings");});
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(window&&parser.isSet("window-size")) { const auto dimensions=parser.value("window-size").split('x'); if(dimensions.size()==2)window->resize(qMax(540,dimensions[0].toInt()),qMax(360,dimensions[1].toInt())); }
    qint64 firstImage=-1,firstFrame=-1; int frames=0;
    QObject::connect(&viewer,&ViewerController::imageChanged,&app,[&]{if(firstImage<0&&!viewer.image().isNull())firstImage=elapsed.elapsed();});
    if(window)QObject::connect(window,&QQuickWindow::frameSwapped,&app,[&]{++frames;if(firstFrame<0&&!viewer.image().isNull())firstFrame=elapsed.elapsed();});
    // An explicit file wins over resume and is remembered only once it decodes successfully.
    if(!parser.positionalArguments().isEmpty())QTimer::singleShot(0,&viewer,[&]{viewer.open(parser.positionalArguments().first());});
    else QTimer::singleShot(0,&viewer,[&]{viewer.restoreWindow();});
    if(parser.isSet("quit-after")) {
        bool valid=false;const int duration=parser.value("quit-after").toInt(&valid);
        if(!valid || duration<100 || duration>3600000)return 2;
        QTimer::singleShot(duration,&app,[&]{
            if(window&&parser.isSet("screenshot"))window->grabWindow().save(parser.value("screenshot"));
            if(parser.isSet("report")) {
                QJsonObject report{{"version",QCoreApplication::applicationVersion()},{"qt",qVersion()},
                    {"decoded_ms",firstImage},{"first_image_frame_ms",firstFrame},{"frames",frames},
                    {"width",viewer.imageWidth()},{"height",viewer.imageHeight()},
                    {"loading",viewer.loading()},{"status",viewer.status()},
                    {"window_slot",viewer.windowSlot()},{"image_path",viewer.filePath()},
                    {"theme",theme.family()},{"theme_mode",theme.mode()},
                    {"graphics_api",window?int(window->rendererInterface()->graphicsApi()):0},
                    {"device_pixel_ratio",window?window->devicePixelRatio():1.0}};
                QFile file(parser.value("report"));if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());
            }
            app.exit(viewer.loading()?3:0);
        });
    }
    return app.exec();
}
