#include "perf_replay.hpp"
#include "glancelot/perf_trace.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/window_slot.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/favorites_model.hpp"
#include "glancelot/folder_history.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "file_grid_wiring.hpp"
#include "file_operation_wiring.hpp"
#include "glancelot/file_operation_service.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "thumbnail_provider.hpp"
#include "native_title_bar.hpp"
#include "startup_intent.hpp"
#include "startup_environment.hpp"
#include "settings_maintenance.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QSysInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>
#include <QIcon>
#include <QFontDatabase>
#include <QDir>
#include <rhi/qrhi.h>
#include <atomic>
#include <mutex>
#include <memory>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
void applicationMetadata() {
    QCoreApplication::setApplicationName("Glancelot");
    QCoreApplication::setOrganizationName("Glancelot");
    QCoreApplication::setApplicationVersion("0.1.0-alpha.1");
}
bool writeOutput(const QByteArray& bytes) {
#ifdef Q_OS_WIN
    // JSON is UTF8 with an explicitly budgeted newline. CRT text expansion
    // must not add an unaccounted CR after bounded serialization.
    static const auto binaryMode=_setmode(_fileno(stdout),_O_BINARY);
    if(binaryMode==-1)return false;
#endif
    return std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size());
}
int startupError(const QString& code,const QString& detail,int exitCode) {
    const QJsonObject result{{"schema",1},{"code",code},{"detail",detail.left(1024)},{"generation","0"}};
    return writeOutput(QJsonDocument(result).toJson(QJsonDocument::Compact)+'\n')?exitCode:7;
}
struct GraphicsEvidence {
    std::atomic_bool observed{};
    std::mutex mutex;
    int backend{-1},deviceType{-1};
    QString deviceName;
};
}
int main(int argc,char** argv) {
    const auto perfStartupBegin=glancelot::perf::Recorder::nowNs();
    const auto arguments=glancelot::startup::originalArguments(argc,argv);
    const auto originalConfig=glancelot::startup::captureOriginalConfig();
    QCommandLineParser parser;glancelot::startup::configure(parser);
    const auto intent=glancelot::startup::parse(arguments,parser);
    using glancelot::startup::Mode;
    if(intent.mode==Mode::Error)return startupError("ArgumentError",intent.error,2);
    if(intent.mode==Mode::Help||intent.mode==Mode::Version||intent.maintenance()) {
        QCoreApplication app(argc,argv);applicationMetadata();
        if(intent.mode==Mode::Help)return writeOutput(parser.helpText().toUtf8())?0:7;
        if(intent.mode==Mode::Version)return writeOutput((QCoreApplication::applicationName()+" "+QCoreApplication::applicationVersion()+"\n").toUtf8())?0:7;
        const auto result=glancelot::startup::maintenance(intent,{glancelot::startup::resolveOriginalConfig(originalConfig)});
        return writeOutput(result.json)?result.exitCode:7;
    }
    glancelot::startup::SafeEnvironment safeEnvironment;
    if(intent.safeMode) {
        safeEnvironment=glancelot::startup::prepareSafeEnvironment();
        if(safeEnvironment.status.code!=glancelot::recovery::Code::Ok)
            return startupError(glancelot::startup::codeName(safeEnvironment.status.code),safeEnvironment.status.detail,glancelot::startup::exitCode(safeEnvironment.status.code));
    }
    QGuiApplication app(argc,argv);
    applicationMetadata();
    const auto settingsNamespace=intent.safeMode?safeEnvironment.context.directory:glancelot::startup::resolveOriginalConfig(originalConfig);
    QQuickStyle::setStyle("Basic");
    // Bundled interface typefaces; Theme.qml falls back to system fonts if any of these fail to register.
    for(const char* face:{"Geist-Light","Geist-Regular","Geist-Medium","Geist-SemiBold","GeistMono-Regular"})
        if(QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)))<0)
            std::fprintf(stderr,"Glancelot: bundled font %s could not be registered; using a system font\n",face);
    if (parser.isSet("perf-report")) glancelot::perf::Recorder::instance().setEnabled(true);
    QElapsedTimer elapsed;elapsed.start();
    if(parser.isSet("reset-settings")){QString moved;if(glancelot::resetSettings(&moved)&&!moved.isEmpty())std::fprintf(stderr,"Glancelot: settings set aside as %s\n",qPrintable(moved));}
    // The slot is claimed before the window shows; it is released on normal exit, and a crashed window's slot is
    // reclaimed by the next claim (pid plus process start time).
    std::unique_ptr<WindowSlot> windowSlot;
    if(!intent.noResume)windowSlot=std::make_unique<WindowSlot>(settingsNamespace,0,safeEnvironment.context.lifetime);
    // Keep ownership until after the controller destructor has finished its final save.
    ViewerController viewer;
    viewer.setSafeMode(intent.safeMode);
    if(windowSlot)viewer.setWindowClaim(windowSlot->claimIdentity());
    QObject::connect(&app,&QGuiApplication::aboutToQuit,&app,[&]{viewer.flushWindowPosition();});
    // If coordination was busy at startup, keep trying for a while instead of silently running without memory.
    QTimer slotRetry;slotRetry.setInterval(4000);int slotAttempts=0;
    if(windowSlot&&windowSlot->number()==0){
        QObject::connect(&slotRetry,&QTimer::timeout,&app,[&]{if(windowSlot->claim(0)){viewer.setWindowClaim(windowSlot->claimIdentity());viewer.restoreWindow();slotRetry.stop();}else if(++slotAttempts>=8)slotRetry.stop();});
        slotRetry.start();
    }
    PaneLayout paneLayout;paneLayout.setSlot(viewer.windowSlot());
    QObject::connect(&viewer,&ViewerController::stateChanged,&paneLayout,[&]{paneLayout.setSlot(viewer.windowSlot());});
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
    // Root enumeration does not list contents. Defer listings until the panel is wanted.
    QObject::connect(&viewer,&ViewerController::stateChanged,&folderTree,[&]{if(viewer.treeVisible()){if(folderTree.count()==0)folderTree.browseComputer();folderTree.sync(viewer.snapshot());}});
    if(viewer.treeVisible())folderTree.browseComputer();
    // Mixed file-manager grid. It follows the controller's folder, sort and opened photo; PhotoGrid activates it
    // only while visible. A directory change is detected by key so ordinary image steps never relist the folder.
    DirectoryEntryModel fileGrid;
    // Shared with the grid UI test harness (src/app/file_grid_wiring.hpp) so tests run the shipped wiring.
    file_grid_wiring::followViewer(viewer,fileGrid);
    // One thumbnail epoch: the service accepts a single epoch, so the grid's URLs advance in lockstep with the
    // filmstrip's (directory changes and on-disk invalidation both pass through FilmstripModel::invalidate).
    file_grid_wiring::lockThumbnailEpoch(filmstrip,fileGrid);
    QObject::connect(&filmstrip,&FilmstripModel::epochChanged,&thumbnails,[&]{thumbnails.setEpoch(filmstrip.epoch());});
    // DELETE-01: the one user-initiated mutation path (Trash/Recycle Bin only). Viewing never reaches it.
    FileOperationService fileOperations;
    file_operation_wiring::follow(fileOperations,viewer,folderTree);
    FavoritesModel favorites;
    favorites.setCurrentDirectory(viewer.directory());
    QObject::connect(&viewer,&ViewerController::stateChanged,&favorites,[&]{favorites.setCurrentDirectory(viewer.directory());});
    FolderHistory folderHistory;
    if (!viewer.directory().isEmpty()) folderHistory.recordVisit(viewer.directory(), viewer.filePath());
    QObject::connect(&viewer, &ViewerController::stateChanged, &folderHistory, [&] {
        if (!viewer.directory().isEmpty()) folderHistory.recordVisit(viewer.directory(), viewer.filePath());
    });
    QObject::connect(&folderHistory, &FolderHistory::navigationRequested, &viewer, [&](const QString& directory, const QString& filePath, const QString& selectedPath, int scroll) {
        Q_UNUSED(selectedPath); Q_UNUSED(scroll);
        if (!filePath.isEmpty() && QFileInfo::exists(filePath)) viewer.open(filePath);
        else if (!directory.isEmpty()) viewer.navigateDirectory(directory);
    });
    QQmlApplicationEngine engine;
    engine.addImageProvider("thumb",new ThumbnailProvider(&thumbnails));
    engine.rootContext()->setContextProperty("viewer",&viewer);
    engine.rootContext()->setContextProperty("paneLayout",&paneLayout);
    engine.rootContext()->setContextProperty("filmstrip",&filmstrip);
    engine.rootContext()->setContextProperty("folderTree",&folderTree);
    engine.rootContext()->setContextProperty("favoritesModel",&favorites);
    engine.rootContext()->setContextProperty("folderHistory",&folderHistory);
    engine.rootContext()->setContextProperty("fileGrid",&fileGrid);
    engine.rootContext()->setContextProperty("theme",&theme);
    engine.rootContext()->setContextProperty("fileOperations",&fileOperations);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(2);},Qt::QueuedConnection);
    engine.loadFromModule("Glancelot","Main");
    if(engine.rootObjects().isEmpty())return 2;
    if(parser.isSet("show-themes"))QTimer::singleShot(100,engine.rootObjects().first(),[&]{QMetaObject::invokeMethod(engine.rootObjects().first(),"openThemes");});
    if(parser.isSet("show-settings"))QTimer::singleShot(100,&viewer,[&]{viewer.trigger("settings");});
    if(parser.isSet("show-grid"))QTimer::singleShot(100,&viewer,[&]{viewer.trigger("grid");});
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    const auto graphicsEvidence=std::make_shared<GraphicsEvidence>();
    if(window) {
        QObject::connect(window,&QQuickWindow::beforeRendering,&app,[window,graphicsEvidence]{
            if(graphicsEvidence->observed.load())return;
            auto* rhi=static_cast<QRhi*>(window->rendererInterface()->getResource(window,QSGRendererInterface::RhiResource));
            if(!rhi)return;
            const auto info=rhi->driverInfo();
            std::lock_guard lock(graphicsEvidence->mutex);
            graphicsEvidence->backend=int(rhi->backend());graphicsEvidence->deviceType=int(info.deviceType);
            graphicsEvidence->deviceName=QString::fromUtf8(info.deviceName.left(256));graphicsEvidence->observed=true;
        },Qt::DirectConnection);
        QObject::connect(window,&QQuickWindow::sceneGraphInvalidated,&app,[graphicsEvidence]{graphicsEvidence->observed=false;},Qt::DirectConnection);
    }
    if(window&&parser.isSet("window-size")) { const auto dimensions=parser.value("window-size").split('x'); if(dimensions.size()==2)window->resize(qMax(540,dimensions[0].toInt()),qMax(360,dimensions[1].toInt())); }
    if(window&&parser.isSet("frameless"))window->setProperty("frameless",true);
    // The decorated title bar follows the theme where the OS allows it (Windows); a no-op elsewhere.
    QPointer<glancelot::NativeTitleBarSync> titleBar=window?new glancelot::NativeTitleBarSync(window,&theme):nullptr;
    // Changing the frameless flag can restyle or recreate the native window; re-apply once Qt has done so.
    QTimer titleBarRefresh;titleBarRefresh.setSingleShot(true);titleBarRefresh.setInterval(0);
    QObject::connect(&titleBarRefresh,&QTimer::timeout,&app,[&]{if(titleBar)titleBar->apply();});
    if(window)QQmlProperty(window,"frameless").connectNotifySignal(&titleBarRefresh,SLOT(start()));
    // PERF-MEASURE-01: frameSwapped is Qt's swap notification, not physical presentation.
    bool perfStartupRecorded=false, perfReportFailed=false;
    if(window) QObject::connect(window,&QQuickWindow::frameSwapped,&app,[&] {
        if(perfStartupRecorded || viewer.image().isNull()) return;
        perfStartupRecorded=true;
        auto& recorder=glancelot::perf::Recorder::instance();
        if(recorder.enabled()) recorder.record({glancelot::perf::Stage::Startup,glancelot::perf::Cold,0,
            std::uint64_t(viewer.image().cacheKey()),perfStartupBegin,glancelot::perf::Recorder::nowNs(),0});
    });
    QObject::connect(&app,&QGuiApplication::aboutToQuit,&app,[&] {
        if(!parser.isSet("perf-report")) return;
        auto& recorder=glancelot::perf::Recorder::instance();
        auto spans=recorder.drain();
        const auto build=QCoreApplication::applicationVersion()+"-"+qEnvironmentVariable("GLANCELOT_PERF_BUILD","unversioned");
        glancelot::perf::ReportContext context{build.toStdString(),
            (QSysInfo::productType()+"-"+QSysInfo::productVersion()+"-"+QSysInfo::kernelVersion()).toStdString(),
            std::string("qt-api-")+std::to_string(window?int(window->rendererInterface()->graphicsApi()):0),
            qEnvironmentVariable("GLANCELOT_PERF_FIXTURE_ID","unspecified").toStdString()};
        const auto json=glancelot::perf::toJsonReport(spans,context,recorder.dropped(),true);
        QSaveFile report(parser.value("perf-report"));
        if(!report.open(QIODevice::WriteOnly) || report.write(json.data(),qint64(json.size()))!=qint64(json.size()) || !report.commit()) {
            perfReportFailed=true; std::fprintf(stderr,"Glancelot: performance report could not be saved\n");
        }
    });
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
                QJsonObject actualRhi;
                {
                    std::lock_guard lock(graphicsEvidence->mutex);
                    const bool cpu=graphicsEvidence->deviceType==int(QRhiDriverInfo::CpuDevice);
                    const auto name=graphicsEvidence->deviceName.toLower();
                    const bool softwareName=name.contains("llvmpipe")||name.contains("softpipe")||name.contains("swrast");
                    actualRhi={{"observed",graphicsEvidence->observed.load()},{"backend",graphicsEvidence->backend},
                        {"device_type",graphicsEvidence->deviceType},{"device_name",graphicsEvidence->deviceName},
                        {"software_detection",cpu?"cpu-device-type":softwareName?"renderer-name":"not-detected"}};
                }
                auto* canvas=engine.rootObjects().first()->findChild<ImageCanvas*>("canvas");
                const auto origin=canvas?canvas->mapToScene(QPointF{}):QPointF{};
                const QJsonObject canvasEvidence{{"x",origin.x()},{"y",origin.y()},
                    {"width",canvas?canvas->width():0},{"height",canvas?canvas->height():0},
                    {"zoom",canvas?canvas->zoom():0},{"visible",canvas&&canvas->isVisible()},
                    {"renderer_error",canvas?canvas->rendererError():QStringLiteral("Canvas unavailable")}};
                QJsonObject report{{"version",QCoreApplication::applicationVersion()},{"qt",qVersion()},
                    {"safe_mode",intent.safeMode},{"settings_directory",settingsNamespace},
                    {"requested_backend",safeEnvironment.requestedBackend},{"rhi",actualRhi},{"canvas",canvasEvidence},
                    {"wheel_mode",viewer.wheelMode()},
                    {"decoded_ms",firstImage},{"first_image_frame_ms",firstFrame},{"frames",frames},
                    {"width",viewer.imageWidth()},{"height",viewer.imageHeight()},
                    {"loading",viewer.loading()},{"status",viewer.status()},
                    {"window_slot",viewer.windowSlot()},{"image_path",viewer.filePath()},
                    {"grid_visible",engine.rootObjects().first()->property("gridMode").toBool()},
                    {"grid_live_cells",engine.rootObjects().first()->findChild<QObject*>("photoGrid") ? engine.rootObjects().first()->findChild<QObject*>("photoGrid")->property("liveDelegateCount").toInt() : 0},
                    {"folder_count",filmstrip.count()},
                    {"thumbnail_pending",thumbnails.pending()},{"thumbnail_active",thumbnails.active()},
                    {"thumbnail_cache_bytes",double(thumbnails.cacheBytes())},
                    {"theme",theme.family()},{"theme_mode",theme.mode()},
                    {"frameless",window?window->property("frameless").toBool():false},{"window_active",window?window->isActive():false},
                    {"native_title_bar",QJsonObject{{"dark_mode",titleBar&&titleBar->last().darkMode},{"caption_color",titleBar&&titleBar->last().caption},
                        {"text_color",titleBar&&titleBar->last().text},{"border_color",titleBar&&titleBar->last().border}}},
                    {"graphics_api",window?int(window->rendererInterface()->graphicsApi()):0},
                    {"device_pixel_ratio",window?window->devicePixelRatio():1.0}};
                QFile file(parser.value("report"));if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());
            }
            app.exit(viewer.loading()?3:0);
        });
    }
    if(!glancelot::perf::installReplay(app,viewer,fileGrid,thumbnails,engine.rootObjects().first(),parser.isSet("perf-report")&&parser.isSet("no-resume")))return 2;
    const int result=app.exec();
    return perfReportFailed ? 4 : result;
}
