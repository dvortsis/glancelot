#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "glancelot/settings_recovery.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/favorites_model.hpp"
#include "file_lock.hpp"
#include "settings_transaction.hpp"
#include "settings_retry.hpp"
#include <QScopeGuard>
#include <QPointer>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class RecoveryControllerTests final:public QObject {
    Q_OBJECT
    static QJsonObject settings(const QString& directory) {
        QFile file(QDir(directory).filePath("settings.json"));
        return file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object():QJsonObject{};
    }
    static QJsonObject record(const QString& directory) {return settings(directory).value("recovery").toObject().value("windows").toObject().value("1").toObject();}
    static QString folder(const QString& directory) {return QDir::fromNativeSeparators(settings(directory).value("windows").toObject().value("1").toObject().value("folder").toString());}
    static bool writePosition(const QString& directory,const QString& folder,const QString& image={}) {
        QFile file(QDir(directory).filePath("settings.json"));
        const QJsonObject root{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",image},{"saved",1},{"future",true}}}}}};
        const auto bytes=QJsonDocument(root).toJson();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
private slots:
    void preferenceSavesRetryWhileOwnCheckpointHoldsWriter() {
        struct Gate {std::mutex mutex;std::condition_variable changed;bool released{};std::atomic_bool entered{};std::atomic_int replacements{};};
        const auto gate=std::make_shared<Gate>();glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[gate](glancelot::settings_detail::Stage stage){
            if(stage!=glancelot::settings_detail::Stage::BeforeReplace||++gate->replacements!=2)return;
            std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.wait(lock,[&]{return gate->released;});
        };
        const auto* previous=glancelot::settings_detail::transactionHooks;glancelot::settings_detail::transactionHooks=&hooks;
        const auto restore=qScopeGuard([previous]{glancelot::settings_detail::transactionHooks=previous;});
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        WindowSlot slot(config.path(),0);ViewerController viewer;
        ThemeManager theme;PaneLayout layout;layout.setSlot(1);FavoritesModel favorites;
        const auto release=qScopeGuard([gate]{std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();});
        viewer.setWindowClaim(slot.claimIdentity());QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"),QJsonValue("dirty"),3000);
        viewer.navigateDirectory(media.path());QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);viewer.flushWindowPosition();
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(),3000);
        const bool tree=!viewer.treeVisible();QElapsedTimer elapsed;elapsed.start();
        viewer.setTreeVisible(tree);viewer.setWheelMode("pan");viewer.setWheelMode("zoom");
        QVERIFY(viewer.setWheelModifier("ctrl","zoom")); QVERIFY(viewer.setWheelModifier("ctrl","pan-horizontal"));
        QVERIFY(viewer.setWheelModifier("shift","pan"));
        theme.setMode("light");QVERIFY(layout.resize("tree",400));QVERIFY(favorites.addFavorite(media.path(),"generated"));
        viewer.setSortKey("size");viewer.setSortDescending(true);QVERIFY(viewer.setBinding("fullscreen","Ctrl+Alt+F11"));
        QVERIFY(elapsed.elapsed()<250);QVERIFY(!viewer.status().contains("could not be saved"));
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("tree"),QJsonValue(tree),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("wheel"),QJsonValue("zoom"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("wheelModifiers").toObject().value("ctrl"),QJsonValue("pan-horizontal"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("wheelModifiers").toObject().value("shift"),QJsonValue("pan"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),media.path(),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("appearance").toObject().value("mode"),QJsonValue("light"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("layouts").toObject().value("1").toObject().value("panes").toObject().value("tree").toObject().value("extent"),QJsonValue(400),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("favorites").toArray().size(),qsizetype(1),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("key"),QJsonValue("size"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("descending"),QJsonValue(true),3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("bindings").toObject().value("fullscreen"),QJsonValue("Ctrl+Alt+F11"),3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout.feedback().isEmpty(),3000);
    }
    void importedModifierAbsenceRestoresDefaultsInTheLiveController() {
        QTemporaryDir config,source; QVERIFY(config.isValid()&&source.isValid());
        qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const auto write=[](const QString& path,const QJsonObject& root){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(root).toJson())>0;};
        QVERIFY(write(config.filePath("settings.json"),{{"schema",1},{"wheelModifiers",QJsonObject{{"ctrl","pan"},{"shift","zoom"},{"alt","pan-horizontal"}}}}));
        QVERIFY(write(source.filePath("settings.json"),{{"schema",1}}));
        ViewerController viewer; QCOMPARE(viewer.wheelModifiers().value("ctrl").toString(),QString("pan"));
        QVERIFY(viewer.importSettings(source.filePath("settings.json")));
        QCOMPARE(viewer.wheelModifiers().value("ctrl").toString(),QString("zoom"));
        QCOMPARE(viewer.wheelModifiers().value("shift").toString(),QString("pan-horizontal"));
        QCOMPARE(viewer.wheelModifiers().value("alt").toString(),viewer.wheelMode());
    }
    void missingSourceLayoutKeepsUnknownsThroughActualLayoutEdit() {
        using namespace glancelot::recovery;
        QTemporaryDir config,source,packages;QVERIFY(config.isValid()&&source.isValid()&&packages.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const QJsonObject original{{"schema",1},{"layouts",QJsonObject{{"1",QJsonObject{{"version",1},{"locked",true},{"future",QJsonObject{}},
            {"center",QJsonObject{{"ratio",0.8},{"future",true}}},{"panes",QJsonObject{{"tree",QJsonObject{{"extent",350},{"future",7}}},{"future-pane",QJsonObject{{"kind",9}}}}}}},
            {"2",QJsonObject{{"version",1},{"locked",true},{"panes",QJsonObject{{"tree",QJsonObject{{"extent",350}}}}}}}}}};
        const auto write=[](const QString& path,const QJsonObject& root){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(root).toJson())>0;};
        QVERIFY(write(config.filePath("settings.json"),original));QVERIFY(write(source.filePath("settings.json"),{{"schema",1}}));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        for(const int slot:{1,2}) {
            const auto proposed=preview({config.path()},captured.destination,OfflineSlotScope{SlotSection::Layout,slot});QVERIFY(proposed.plan.applicable());
            QCOMPARE(apply(proposed.plan).status.code,Code::Committed);
        }
        auto layouts=settings(config.path()).value("layouts").toObject();QVERIFY(!layouts.contains("2"));
        QCOMPARE(layouts.value("1").toObject().value("version"),QJsonValue(1));
        PaneLayout layout;layout.setSlot(1);QVERIFY(layout.feedback().isEmpty());QVERIFY(layout.resize("tree",420));
        const auto current=settings(config.path()).value("layouts").toObject().value("1").toObject();
        QVERIFY(current.contains("future"));QCOMPARE(current.value("center").toObject().value("future"),QJsonValue(true));
        QCOMPARE(current.value("panes").toObject().value("tree").toObject().value("future"),QJsonValue(7));
        QCOMPARE(current.value("panes").toObject().value("tree").toObject().value("extent"),QJsonValue(420));
        QCOMPARE(current.value("panes").toObject().value("future-pane"),QJsonValue(QJsonObject{{"kind",9}}));
    }
    void deferredSaveRetainsNamespaceAndFinalizesWithoutDyingOwnerCallbacks() {
        QTemporaryDir first,second;QVERIFY(first.isValid()&&second.isValid());qputenv("GLANCELOT_CONFIG_DIR",first.path().toUtf8());
        FileLock held(first.filePath("settings.json.lock"));QVERIFY(held.acquire(0));QObject owner;
        int completed=0;
        QVERIFY(glancelot::settings_detail::saveWithRetry(&owner,"tree",[](QJsonObject& root){root["tree"]=false;},[&](auto result){QVERIFY(result.ok());++completed;}));
        auto closing=std::make_unique<QObject>();int dyingCallbacks=0;
        QVERIFY(glancelot::settings_detail::saveWithRetry(closing.get(),"wheel",[](QJsonObject& root){root["wheel"]="pan";},[&](auto){++dyingCallbacks;}));
        qputenv("GLANCELOT_CONFIG_DIR",second.path().toUtf8());held.release();closing.reset();QCOMPARE(dyingCallbacks,0);
        QTRY_COMPARE_WITH_TIMEOUT(completed,1,3000);QCOMPARE(settings(first.path()).value("tree"),QJsonValue(false));
        QCOMPARE(settings(first.path()).value("wheel"),QJsonValue("pan"));QVERIFY(!QFile::exists(second.filePath("settings.json")));
    }
    void deferredShortcutStillChecksFreshForeignConflicts() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        ViewerController viewer;viewer.resetBindings();FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        QVERIFY(viewer.setBinding("fullscreen","Ctrl+Alt+F11"));held.release();
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){auto bindings=root.value("bindings").toObject();bindings["rotate"]="Ctrl+Alt+F11";root["bindings"]=bindings;}).ok());
        const auto bytes=[](const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};};
        const auto primary=bytes(config.filePath("settings.json")),backup=bytes(config.filePath("settings.json.bak"));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("already assigned"),3000);
        QCOMPARE(viewer.bindingFor("fullscreen"),QString("F11"));
        const auto bindings=settings(config.path()).value("bindings").toObject();
        QCOMPARE(bindings.value("rotate"),QJsonValue("Ctrl+Alt+F11"));QCOMPARE(bindings.value("fullscreen"),QJsonValue("F11"));
        QCOMPARE(bytes(config.filePath("settings.json")),primary);QCOMPARE(bytes(config.filePath("settings.json.bak")),backup);
    }
    void deferredLayoutIsVisibleAfterLeavingAndReturningToSlot() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout layout;layout.setSlot(1);QVERIFY(layout.resize("tree",300));
        FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        QVERIFY(layout.resize("tree",410));layout.setSlot(2);layout.setSlot(1);
        QCOMPARE(layout.pane("tree").value("extent"),QVariant(410));
        held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("layouts").toObject().value("1").toObject().value("panes").toObject().value("tree").toObject().value("extent"),QJsonValue(410),3000);
        QTRY_VERIFY_WITH_TIMEOUT(layout.feedback().isEmpty(),3000);QCOMPARE(layout.pane("tree").value("extent"),QVariant(410));
    }
    void supersededBindingConflictRestoresPersistedValue() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        ViewerController viewer;viewer.resetBindings();FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        QVERIFY(viewer.setBinding("fullscreen","Ctrl+Alt+F10"));QVERIFY(viewer.setBinding("fullscreen","Ctrl+Alt+F11"));held.release();
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){auto bindings=root.value("bindings").toObject();bindings["fullscreen"]="Ctrl+Alt+F12";bindings["rotate"]="Ctrl+Alt+F11";root["bindings"]=bindings;}).ok());
        QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("already assigned"),3000);
        QCOMPARE(viewer.bindingFor("fullscreen"),QString("Ctrl+Alt+F12"));
        QCOMPARE(settings(config.path()).value("bindings").toObject().value("fullscreen"),QJsonValue("Ctrl+Alt+F12"));
    }
    void pendingSortStatusTracksPersistence_data() {
        QTest::addColumn<bool>("loaded");QTest::newRow("empty")<<false;QTest::newRow("loaded")<<true;
    }
    void pendingSortStatusTracksPersistence() {
        QFETCH(bool,loaded);
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        ViewerController viewer;
        if(loaded){QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::green);const auto path=media.filePath("generated.png");QVERIFY(image.save(path));viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),5000);}
        QVERIFY(QDir().mkpath(config.path()));FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        viewer.setSortKey("size");
        if(loaded)QTRY_VERIFY_WITH_TIMEOUT(viewer.status().startsWith("Sorted by size"),3000);
        QVERIFY(viewer.status().contains("for this session"));QVERIFY(viewer.status().contains("pending"));QVERIFY(!viewer.status().contains("could not be saved"));
        QVERIFY(!settings(config.path()).value("sort").toObject().contains("key"));held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("key"),QJsonValue("size"),3000);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.status().contains("for this session"),3000);QVERIFY(!viewer.status().contains("pending"));
    }
    void sortCompletionPreservesNewerFeedback() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        ViewerController viewer;FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));viewer.setSortKey("size");
        viewer.navigateDirectory(media.filePath("missing"));QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);
        const auto newer=viewer.status();QVERIFY(!newer.contains("pending"));held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("key"),QJsonValue("size"),3000);QCOMPARE(viewer.status(),newer);
    }
    void failedSortIntentIsRetriedWithNextEditAndForeignFieldsPreserved() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["sort"]=QJsonObject{{"key","name"},{"future",7},{"seed",123}};}).ok());
        ViewerController viewer;FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));viewer.setSortKey("size");
        QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("could not be saved"),5000);held.release();
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){auto sort=root.value("sort").toObject();sort["key"]="name";sort["seed"]=789;root["sort"]=sort;}).ok());
        viewer.setSortDescending(true);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("key"),QJsonValue("size"),3000);
        const auto saved=settings(config.path()).value("sort").toObject();QCOMPARE(saved.value("descending"),QJsonValue(true));
        QCOMPARE(saved.value("seed"),QJsonValue(789));QCOMPARE(saved.value("future"),QJsonValue(7));QVERIFY(!viewer.status().contains("could not be saved"));
    }
    void retryNoChangeStillRepairsRecoveredPrimary() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const auto write=[](const QString& path,const QByteArray& bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();};
        const auto bytes=[](const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};};
        const QByteArray corrupt="{corrupt",backup="{\"schema\":1,\"tree\":true}";
        QVERIFY(write(config.filePath("settings.json"),corrupt));QVERIFY(write(config.filePath("settings.json.bak"),backup));
        QObject owner;int completed=0;
        QVERIFY(glancelot::settings_detail::saveWithRetry(&owner,"tree",[](QJsonObject& root){root["tree"]=true;},[&](auto result){QVERIFY(result.ok());++completed;}));
        QCOMPARE(completed,1);QCOMPARE(settings(config.path()).value("tree"),QJsonValue(true));QCOMPARE(bytes(config.filePath("settings.json.damaged")),corrupt);
        const auto primary=bytes(config.filePath("settings.json")),lastGood=bytes(config.filePath("settings.json.bak"));
        QVERIFY(glancelot::settings_detail::saveWithRetry(&owner,"tree",[](QJsonObject& root){root["tree"]=true;}));
        QCOMPARE(bytes(config.filePath("settings.json")),primary);QCOMPARE(bytes(config.filePath("settings.json.bak")),lastGood);
    }
    void deferredLayoutCompletionPreservesCurrentSlotFeedback() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout layout;layout.setSlot(1);QVERIFY(layout.resize("tree",300));
        FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        QVERIFY(layout.resize("tree",410));layout.setSlot(2);layout.setLocked(true);
        QVERIFY(!layout.resize("tree",420));const auto feedback=layout.feedback();QVERIFY(feedback.contains("Unlock"));
        held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("layouts").toObject().value("2").toObject().value("locked"),QJsonValue(true),3000);
        QCOMPARE(layout.feedback(),feedback);QVERIFY(layout.locked());
    }
    void deferredLayoutDoesNotOverlayAnotherNamespace() {
        QTemporaryDir first,second;QVERIFY(first.isValid()&&second.isValid());qputenv("GLANCELOT_CONFIG_DIR",first.path().toUtf8());
        PaneLayout layout;layout.setSlot(1);QVERIFY(layout.resize("tree",300));
        FileLock held(first.filePath("settings.json.lock"));QVERIFY(held.acquire(0));QVERIFY(layout.resize("tree",410));
        qputenv("GLANCELOT_CONFIG_DIR",second.path().toUtf8());layout.setSlot(2);layout.setSlot(1);
        QVERIFY(layout.pane("tree").value("extent").toInt()!=410);const auto feedback=layout.feedback();held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(first.path()).value("layouts").toObject().value("1").toObject().value("panes").toObject().value("tree").toObject().value("extent"),QJsonValue(410),3000);
        QCOMPARE(layout.feedback(),feedback);QVERIFY(!QFile::exists(second.filePath("settings.json")));
    }
    void sortBurstCoalescesWithoutLosingChangedFields() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["sort"]=QJsonObject{{"future",7},{"seed",123}};}).ok());
        ViewerController viewer;FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        for(int i=0;i<200;++i){viewer.setSortKey(i%2?"size":"name");viewer.setSortDescending(i%2);}
        QVERIFY(!viewer.status().contains("could not be saved"));QCOMPARE(viewer.sortKey(),QString("size"));QVERIFY(viewer.sortDescending());held.release();
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("sort").toObject().value("key"),QJsonValue("size"),3000);
        const auto saved=settings(config.path()).value("sort").toObject();QCOMPARE(saved.value("descending"),QJsonValue(true));
        QCOMPARE(saved.value("future"),QJsonValue(7));QCOMPARE(saved.value("seed"),QJsonValue(123));
    }
    void beginRecoveryRetryRefreshesUnavailableFeedback() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),media.path()));
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["recovery"]=QJsonObject{{"version",2}};}).ok());
        WindowSlot slot(config.path(),0);ViewerController viewer;viewer.navigateDirectory(media.path());QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);
        viewer.setWindowClaim(slot.claimIdentity());QTRY_VERIFY_WITH_TIMEOUT(viewer.status().startsWith("Browsing recovery is unavailable"),3000);
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root.remove("recovery");}).ok());
        QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"),QJsonValue("dirty"),3000);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.status(),QString("Browsing recovery is available."),3000);
        QCOMPARE(folder(config.path()),media.path());
    }
    void retryExhaustionReportsFailureAndCompletionMayDeleteOwner() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["tree"]=true;}).ok());
        auto viewer=std::make_unique<ViewerController>();QPointer<ViewerController> alive(viewer.get());
        FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));viewer->setTreeVisible(false);
        connect(viewer.get(),&ViewerController::stateChanged,this,[&]{if(viewer&&viewer->status().contains("could not be saved"))viewer.reset();});
        int ticks=0;QTimer heartbeat;heartbeat.setInterval(10);connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start();
        QTRY_VERIFY_WITH_TIMEOUT(!alive,5000);QVERIFY(ticks>20);QCOMPARE(settings(config.path()).value("tree"),QJsonValue(true));
    }
    void deferredBindingNotificationMayDeleteOwner() {
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        auto viewer=std::make_unique<ViewerController>();viewer->resetBindings();QPointer<ViewerController> alive(viewer.get());
        FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));QVERIFY(viewer->setBinding("fullscreen","Ctrl+Alt+F11"));
        connect(viewer.get(),&ViewerController::bindingsChanged,this,[&]{viewer.reset();});held.release();
        QTRY_VERIFY_WITH_TIMEOUT(!alive,3000);
        QCOMPARE(settings(config.path()).value("bindings").toObject().value("fullscreen"),QJsonValue("Ctrl+Alt+F11"));
    }
    void malformedRecoveryStillAllowsValidatedReadOnlyResume_data() {
        QTest::addColumn<QJsonValue>("recovery");
        QTest::newRow("future")<<QJsonValue(QJsonObject{{"version",2}});
        QTest::newRow("malformed-record")<<QJsonValue(QJsonObject{{"version",1},{"windows",QJsonObject{{"1","bad"}}}});
    }
    void malformedRecoveryStillAllowsValidatedReadOnlyResume() {
        QFETCH(QJsonValue,recovery);QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const auto imagePath=media.filePath("generated.png");QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::green);QVERIFY(image.save(imagePath));
        QVERIFY(writePosition(config.path(),media.path(),imagePath));auto root=settings(config.path());root["recovery"]=recovery;
        {QFile file(config.filePath("settings.json"));QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(QJsonDocument(root).toJson())>0);}
        WindowSlot slot(config.path(),0);
        {ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());viewer.restoreWindow();
         QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),5000);QCOMPARE(viewer.filePath(),imagePath);}
        QCOMPARE(settings(config.path()).value("recovery"),recovery);QCOMPARE(folder(config.path()),media.path());
    }
    void successfulEmptyDirectoryIsCheckpointedAndCleaned() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());viewer.navigateDirectory(media.path());
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(viewer.directoryError().isEmpty());
            viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),media.path(),3000);
            QCOMPARE(record(config.path()).value("state"),QJsonValue("dirty"));QCOMPARE(record(config.path()).value("origin"),QJsonValue("navigation"));
            QVERIFY(viewer.filePath().isEmpty());
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void failedExplicitVisitBeforeLateClaimPreventsResume() {
        QTemporaryDir config,oldMedia;QVERIFY(config.isValid()&&oldMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.navigateDirectory(oldMedia.filePath("does-not-exist"));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(!viewer.directoryError().isEmpty());
            viewer.setWindowClaim(slot.claimIdentity());viewer.restoreWindow();
            QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"),QJsonValue("dirty"),3000);
            QTest::qWait(150);QVERIFY(viewer.filePath().isEmpty());QVERIFY(viewer.directory().isEmpty());QVERIFY(!viewer.directoryError().isEmpty());
            QCOMPARE(folder(config.path()),oldMedia.path());
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void explicitVisitWinsWhileBeginWaitsAndUiKeepsResponding() {
        QTemporaryDir config,oldMedia,newMedia;QVERIFY(config.isValid()&&oldMedia.isValid()&&newMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
            viewer.setWindowClaim(slot.claimIdentity());viewer.restoreWindow();
            viewer.navigateDirectory(newMedia.path());QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(viewer.directoryError().isEmpty());
            QCOMPARE(QDir::fromNativeSeparators(viewer.directory()),newMedia.path());QCOMPARE(folder(config.path()),oldMedia.path());
            held.release();viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),newMedia.path(),3000);
            QCOMPARE(settings(config.path()).value("windows").toObject().value("1").toObject().value("future"),QJsonValue(true));
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void decodedSuccessPersistsAndFailedDecodeKeepsCheckpoint() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const auto imagePath=media.filePath("generated.png");QImage image(24,24,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(imagePath));
        WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());viewer.open(imagePath);
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),5000);QVERIFY2(viewer.imageReady(),qPrintable(viewer.status()));viewer.flushWindowPosition();
            QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("windows").toObject().value("1").toObject().value("image"),QJsonValue(imagePath),3000);
            const auto checkpoint=record(config.path()).value("checkpointRevision");
            viewer.open(media.filePath("missing.png"));QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),5000);QVERIFY(!viewer.imageReady());viewer.flushWindowPosition();
            QCOMPARE(record(config.path()).value("checkpointRevision"),checkpoint);
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void reentrantExplicitVisitWinsWhenResumeAnnouncesStatus() {
        QTemporaryDir config,oldMedia,newMedia;QVERIFY(config.isValid()&&oldMedia.isValid()&&newMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());bool replaced=false;
        connect(&viewer,&ViewerController::stateChanged,&viewer,[&]{
            if(!replaced&&viewer.status().startsWith("Restoring")){replaced=true;viewer.navigateDirectory(newMedia.path());}
        });
        viewer.restoreWindow();QTRY_VERIFY_WITH_TIMEOUT(replaced,3000);QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);
        QCOMPARE(QDir::fromNativeSeparators(viewer.directory()),newMedia.path());viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),newMedia.path(),3000);
    }
    void claimReplacementWaitsForOutgoingWorkerWithoutBlockingUi() {
        struct Gate {std::mutex mutex;std::condition_variable changed;bool released{};std::atomic_bool entered{};std::atomic_int replacements{};};
        const auto gate=std::make_shared<Gate>();glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[gate](glancelot::settings_detail::Stage stage){
            if(stage!=glancelot::settings_detail::Stage::BeforeReplace||++gate->replacements!=1)return;
            std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.wait(lock,[&]{return gate->released;});
        };
        const auto* previous=glancelot::settings_detail::transactionHooks;glancelot::settings_detail::transactionHooks=&hooks;
        const auto restore=qScopeGuard([previous]{glancelot::settings_detail::transactionHooks=previous;});
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        WindowSlot first(config.path(),0),second(config.path(),0);QCOMPARE(first.number(),1);QCOMPARE(second.number(),2);
        ViewerController viewer;
        const auto release=qScopeGuard([gate]{std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();});
        viewer.setWindowClaim(first.claimIdentity());QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(),3000);
        QElapsedTimer elapsed;elapsed.start();viewer.setWindowClaim(second.claimIdentity());
        qInfo()<<"claim reassignment while outgoing transaction blocked:"<<elapsed.elapsed()<<"ms";
        QVERIFY(elapsed.elapsed()<750);QCOMPARE(viewer.windowSlot(),0);
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
        QTRY_COMPARE_WITH_TIMEOUT(viewer.windowSlot(),2,3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("recovery").toObject().value("windows").toObject().value("2").toObject().value("state"),QJsonValue("dirty"),3000);
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void ownerCanBeDestroyedWhenResumeAnnouncesStatus() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());QVERIFY(writePosition(config.path(),media.path()));
        WindowSlot slot(config.path(),0);auto viewer=std::make_unique<ViewerController>();QPointer<ViewerController> alive(viewer.get());
        viewer->setWindowClaim(slot.claimIdentity());
        connect(viewer.get(),&ViewerController::stateChanged,viewer.get(),[&]{if(viewer&&viewer->status().startsWith("Restoring"))viewer.reset();});
        viewer->restoreWindow();QTRY_VERIFY_WITH_TIMEOUT(!alive,3000);
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void safeFeedbackUsesExistingStatusAndSuppressesResume() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());QVERIFY(writePosition(config.path(),media.path()));
        ViewerController viewer;viewer.setWindowSlot(1);viewer.setSafeMode(true);viewer.restoreWindow();
        QVERIFY(viewer.status().startsWith("Safe mode"));QVERIFY(viewer.directory().isEmpty());QVERIFY(viewer.filePath().isEmpty());
        viewer.setSafeMode(false);QVERIFY(!viewer.status().startsWith("Safe mode"));
    }
};
QTEST_MAIN(RecoveryControllerTests)
#include "recovery_controller_tests.moc"
