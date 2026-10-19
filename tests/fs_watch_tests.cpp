#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include "fs_revision.hpp"
#include "fs_monitor.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "file_grid_wiring.hpp"
#include <QSaveFile>
#include <QImage>
#include <atomic>
#include <thread>

class FSWatchTests : public QObject {
    Q_OBJECT
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QByteArray hash(const QString& path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    }
    QTemporaryDir config;
    static bool png(const QString& path,Qt::GlobalColor color) {
        QSaveFile target(path);if(!target.open(QIODevice::WriteOnly))return false;
        QImage image(32,24,QImage::Format_RGB32);image.fill(color);
        return image.save(&target,"PNG")&&target.commit();
    }
private slots:
    void initTestCase(){QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());}
    void unavailableNativeRegistrationFallsBack() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QVector<fs_watch::Observation> values;
        fs_watch::MonitorOptions options;options.registerNative=false;
        fs_watch::Monitor monitor(this,[&](auto value){values.push_back(std::move(value));},options);
        monitor.setScope(dir.path(),{});QTRY_VERIFY_WITH_TIMEOUT(!values.isEmpty(),5000);
        QVERIFY(!values.last().coverage);const auto file=dir.filePath("external.png");QVERIFY(write(file,"generated"));
        QTRY_VERIFY_WITH_TIMEOUT(values.last().after->entries.contains(fs_watch::key(file)),5000);
        QVERIFY(!values.last().fileEvent);QCOMPARE(monitor.stats().maxHandles,0);
    }
    void delayedOldScopeAndStormAreBounded() {
        QTemporaryDir a,b;QVERIFY(a.isValid()&&b.isValid());
        auto entered=std::make_shared<std::atomic_bool>(false);
        fs_watch::MonitorOptions options;
        options.read=[entered,slow=a.path()](const QString& path,std::stop_token stop) {
            if(path==slow&&!entered->exchange(true))QThread::msleep(300);
            return fs_watch::snapshot(path,stop);
        };
        QVector<fs_watch::Observation> accepted;
        fs_watch::Monitor* current=nullptr;
        fs_watch::Monitor monitor(this,[&](auto value){if(value.scope==current->generation())accepted.push_back(std::move(value));},options);
        current=&monitor;monitor.setScope(a.path(),{});QTRY_VERIFY_WITH_TIMEOUT(entered->load(),5000);
        for(int i=0;i<1000;++i)monitor.setScope(i%2?a.path():b.path(),{});
        const auto newest=monitor.setScope(b.path(),{});
        QTRY_VERIFY_WITH_TIMEOUT(!accepted.isEmpty()&&accepted.last().scope==newest,5000);
        for(const auto& value:accepted)QCOMPARE(value.scope,newest);
        QVERIFY(monitor.stats().snapshots<=4);QVERIFY(monitor.stats().maxHandles<=3);
    }
    void establishedWatchLossAndRearm() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        auto gate=std::make_shared<std::atomic_bool>(true);
        fs_watch::MonitorOptions options;options.registrationGate=gate;
        QVector<fs_watch::Observation> values;
        fs_watch::Monitor monitor(this,[&](auto value){values.push_back(std::move(value));},options);
        monitor.setScope(dir.path(),{});QTRY_VERIFY_WITH_TIMEOUT(!values.isEmpty()&&values.last().coverage,5000);
        gate->store(false);QTRY_VERIFY_WITH_TIMEOUT(!values.last().coverage,5000);
        const auto file=dir.filePath("missed-event.png");QVERIFY(write(file,"disposable"));
        QTRY_VERIFY_WITH_TIMEOUT(values.last().after->entries.contains(fs_watch::key(file)),5000);
        QVERIFY(!values.last().fileEvent);QVERIFY(!values.last().coverage);
        gate->store(true);QTRY_VERIFY_WITH_TIMEOUT(values.last().coverage,10000);
        QVERIFY(monitor.stats().maxHandles<=3);
    }
    void continuousNativeChangesCannotStarvePublication() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("storm.bin");QVERIFY(write(path,"initial"));
        QVector<fs_watch::Observation> values;
        fs_watch::Monitor monitor(this,[&](auto value){values.push_back(std::move(value));});
        monitor.setScope(dir.path(),path);QTRY_VERIFY_WITH_TIMEOUT(!values.isEmpty(),5000);
        const auto initial=values.size();QElapsedTimer elapsed;elapsed.start();
        std::jthread writer([path](std::stop_token stop){
            for(int i=0;i<150&&!stop.stop_requested();++i){write(path,QByteArray::number(i));QThread::msleep(10);}
        });
        QTRY_VERIFY_WITH_TIMEOUT(values.size()>initial,3000);
        QVERIFY2(elapsed.elapsed()<1200,"continuous events postponed publication past maximum debounce plus scheduling allowance");
        writer.request_stop();writer.join();QVERIFY(monitor.stats().maxHandles<=3);
    }
    void blockedProbeDoesNotBlockClose() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        auto entered=std::make_shared<std::atomic_bool>(false),finished=std::make_shared<std::atomic_bool>(false);
        fs_watch::MonitorOptions options;
        options.read=[entered,finished](const QString& path,std::stop_token stop) {
            if(!entered->exchange(true)){QThread::msleep(1500);finished->store(true);}
            return fs_watch::snapshot(path,stop);
        };
        auto monitor=std::make_unique<fs_watch::Monitor>(this,[](auto){},options);
        monitor->setScope(dir.path(),{});QTRY_VERIFY_WITH_TIMEOUT(entered->load(),5000);
        QElapsedTimer elapsed;elapsed.start();monitor.reset();QVERIFY(elapsed.elapsed()<750);
        QTRY_VERIFY_WITH_TIMEOUT(finished->load(),3000);
    }
    void rapidExplicitNavigationCannotRestoreOldPixels() {
        QTemporaryDir a,b;QVERIFY(a.isValid()&&b.isValid());
        const auto first=a.filePath("a.png"),last=b.filePath("b.png");QVERIFY(png(first,Qt::red));QVERIFY(png(last,Qt::blue));
        ViewerController viewer;
        for(int i=0;i<20;++i){viewer.open(first);viewer.open(last);}
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&viewer.filePath()==last,10000);
        QTest::qWait(500);QCOMPARE(viewer.filePath(),last);QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::blue));
    }
    void replacementAndUnrelatedChanges() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto image=dir.filePath("current.png");
        QVERIFY(png(image,Qt::red));ViewerController viewer;viewer.open(image);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::red));
        QTest::qWait(250);QSignalSpy images(&viewer,&ViewerController::imageChanged),invalidated(&viewer,&ViewerController::filesInvalidated);
        QVERIFY(write(dir.filePath("unrelated.txt"),"generated"));
        QTRY_VERIFY_WITH_TIMEOUT(!invalidated.isEmpty(),5000);QTest::qWait(250);
        QCOMPARE(images.size(),0);QCOMPARE(viewer.filePath(),image);
        for(const auto color:{Qt::green,Qt::blue,Qt::yellow}) {
            QVERIFY(png(image,color));
            QTRY_COMPARE_WITH_TIMEOUT(viewer.image().pixelColor(0,0),QColor(color),10000);
        }
        const auto finalHash=hash(image);QTest::qWait(300);QCOMPARE(hash(image),finalHash);
    }
    void emptyFolderAndRenamedDirectoryRecover() {
        QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir(root.path()).mkdir("album"));
        const auto album=root.filePath("album"),moved=root.filePath("moved");
        ViewerController viewer;DirectoryEntryModel grid;file_grid_wiring::followViewer(viewer,grid);grid.setActive(true);
        viewer.navigateDirectory(album);
        QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending()&&fs_watch::key(viewer.directory())==fs_watch::key(album),10000);
        QCOMPARE(viewer.fileCount(),0);QTest::qWait(250);
        const auto a=album+"/a.png",b=album+"/b.png";
        QVERIFY(png(a,Qt::red));QVERIFY(png(b,Qt::blue));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.fileCount()==2&&!grid.loading()&&grid.count()==2,5000);
        viewer.open(a);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);
        grid.select(grid.rowOf(b));QTest::qWait(250);
        const auto identity=fs_watch::probe(fs_watch::native(album)).identity();
        QVERIFY(QDir().rename(album,moved));
        if(!identity.isEmpty()) {
            QTRY_COMPARE_WITH_TIMEOUT(viewer.filePath(),moved+"/a.png",10000);
            QTRY_VERIFY_WITH_TIMEOUT(!grid.loading()&&grid.rowOf(moved+"/b.png")>=0,5000);
            QCOMPARE(grid.selectedPaths(),QStringList{moved+"/b.png"});
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(viewer.filePath()!=a||!viewer.directoryError().isEmpty(),10000);
        }
    }
    void selectedNoncurrentRenameAndCurrentRename() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto a=dir.filePath("a.png"),b=dir.filePath("b.png"),c=dir.filePath("c.png");
        QVERIFY(png(a,Qt::red));QVERIFY(png(b,Qt::blue));
        ViewerController viewer;DirectoryEntryModel grid;file_grid_wiring::followViewer(viewer,grid);grid.setActive(true);viewer.open(a);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&!grid.loading()&&grid.count()==2,10000);
        grid.select(grid.rowOf(b));QCOMPARE(grid.selectedPaths(),QStringList{b});
        const auto identity=fs_watch::probe(fs_watch::native(b)).identity();
        QTest::qWait(250);QVERIFY(QFile::rename(b,c));
        QTRY_VERIFY_WITH_TIMEOUT(grid.rowOf(c)>=0&&!grid.loading(),5000);
        if(!identity.isEmpty())QCOMPARE(grid.selectedPaths(),QStringList{c});
        QCOMPARE(viewer.filePath(),a);
        const auto moved=dir.filePath("renamed-current.png");const auto original=hash(a);
        const auto currentIdentity=fs_watch::probe(fs_watch::native(a)).identity();QVERIFY(QFile::rename(a,moved));
        if(!currentIdentity.isEmpty())QTRY_COMPARE_WITH_TIMEOUT(viewer.filePath(),moved,10000);
        QCOMPARE(hash(moved),original);
    }
    void currentCaseOnlyRenameRetainsNavigation() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto source=dir.filePath("photo.png"),target=dir.filePath("PHOTO.png");
        QVERIFY(png(source,Qt::red));const auto original=hash(source);
        ViewerController viewer;viewer.open(source);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);QTest::qWait(250);
        std::error_code error;std::filesystem::rename(fs_watch::native(source),fs_watch::native(target),error);QVERIFY2(!error,error.message().c_str());
        QTRY_COMPARE_WITH_TIMEOUT(viewer.filePath(),target,10000);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&viewer.currentIndex()>=0,10000);QCOMPARE(hash(target),original);
    }
    void localDeleteDoesNotAdvanceTwiceAndRefreshCommand() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto a=dir.filePath("a.png"),b=dir.filePath("b.png"),c=dir.filePath("c.png");
        for(const auto& name:{a,b,c})QVERIFY(png(name,Qt::red));
        ViewerController viewer;viewer.open(b);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&viewer.fileCount()==3,10000);
        QTest::qWait(250);QVERIFY(QFile::remove(b));viewer.reconcileRemovedPaths({b});
        QTRY_COMPARE_WITH_TIMEOUT(viewer.filePath(),c,10000);QTest::qWait(700);QCOMPARE(viewer.filePath(),c);
        QCOMPARE(viewer.bindingFor("refresh"),QString("F5"));
        QSignalSpy invalidated(&viewer,&ViewerController::filesInvalidated);viewer.trigger("refresh");
        QTRY_VERIFY_WITH_TIMEOUT(!invalidated.isEmpty(),5000);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);
        QCOMPARE(viewer.filePath(),c);
    }
    void failedSourceAndRefreshPreserveWidenedRoot() {
        QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir(root.path()).mkdir("album"));
        const auto photo=root.filePath("album/photo.png");QVERIFY(png(photo,Qt::red));
        ViewerController viewer;viewer.open(photo);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);
        viewer.raiseRoot();QTRY_COMPARE_WITH_TIMEOUT(fs_watch::key(fs_watch::display(viewer.snapshot().root)),fs_watch::key(root.path()),10000);
        viewer.open(root.filePath("album/missing.png"));QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),10000);
        QCOMPARE(fs_watch::key(fs_watch::display(viewer.snapshot().root)),fs_watch::key(root.path()));
        viewer.trigger("refresh");QTRY_COMPARE_WITH_TIMEOUT(viewer.filePath(),photo,10000);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(),10000);
        QCOMPARE(fs_watch::key(fs_watch::display(viewer.snapshot().root)),fs_watch::key(root.path()));
    }
    void nativeEventsRefreshAndScopeIsolation() {
        QTemporaryDir a,b;QVERIFY(a.isValid()&&b.isValid());
        const auto image=a.filePath("current.png");QVERIFY(write(image,"old generated"));
        QVector<fs_watch::Observation> observations;
        fs_watch::Monitor monitor(this,[&](auto value){observations.push_back(std::move(value));});
        const auto first=monitor.setScope(a.path(),image);
        QTRY_VERIFY_WITH_TIMEOUT(!observations.isEmpty(),5000);
        QVERIFY(observations.last().coverage);QCOMPARE(observations.last().scope,first);
        const auto baseline=observations.size();
        QVERIFY(write(image,"replacement generated pixels"));
        QTRY_VERIFY_WITH_TIMEOUT(observations.size()>baseline,5000);
        QVERIFY(observations.last().fileEvent);
        const auto second=monitor.setScope(b.path(),{});QVERIFY(second>first);
        QTRY_COMPARE_WITH_TIMEOUT(observations.last().scope,second,5000);
        monitor.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(observations.last().forced,5000);
        QCOMPARE(observations.last().directory,b.path());
        QVERIFY(monitor.stats().maxHandles<=3);
    }
    void identityRenameAndSourceIntegrity() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto a=dir.filePath("generated-a.png"),b=dir.filePath("generated-b.png");
        QVERIFY(write(a,"disposable fixture bytes"));const auto original=hash(a);
        const auto before=fs_watch::snapshot(dir.path());QVERIFY(before.complete);
        QVERIFY(QFile::rename(a,b));const auto after=fs_watch::snapshot(dir.path());QVERIFY(after.complete);
        const auto id=before.entries[fs_watch::key(a)].revision.identity();
        // Filesystems lacking a native creation identity explicitly refuse rename inference.
        if(id.isEmpty())QVERIFY(fs_watch::renames(before,after).isEmpty());
        else QCOMPARE(fs_watch::renames(before,after).value(fs_watch::key(a)),b);
        QCOMPARE(hash(b),original);
    }
    void atomicReplacementHasNewIdentity() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto image=dir.filePath("image.png"),replacement=dir.filePath("replacement.tmp");
        QVERIFY(write(image,"old generated pixels"));const auto before=fs_watch::probe(fs_watch::native(image));
        QVERIFY(write(replacement,"new generated pixels"));QVERIFY(QFile::remove(image));QVERIFY(QFile::rename(replacement,image));
        const auto after=fs_watch::probe(fs_watch::native(image));QVERIFY(before.known&&after.known);
        if(!before.identity().isEmpty())QVERIFY(before.identity()!=after.identity());
        QVERIFY(!(before==after));
    }
    void sameSizeRestoredMtimeStillHasChangedRevision() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto file=dir.filePath("revision.png");
        QVERIFY(write(file,"old generated"));const auto path=fs_watch::native(file);
        const auto timestamp=std::filesystem::last_write_time(path);const auto before=fs_watch::probe(path);
        QTest::qWait(25);QVERIFY(write(file,"new generated"));std::filesystem::last_write_time(path,timestamp);
        const auto after=fs_watch::probe(path);QVERIFY(before.known&&after.known);
        QCOMPARE(before.size,after.size);QCOMPARE(before.modified,after.modified);
        QVERIFY(before.changed!=after.changed);
    }
    void hardLinksRefuseInference() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto a=dir.filePath("a.png"),b=dir.filePath("b.png");QVERIFY(write(a,"generated"));
        std::error_code error;std::filesystem::create_hard_link(fs_watch::native(a),fs_watch::native(b),error);
        QVERIFY2(!error,error.message().c_str());
        const auto before=fs_watch::snapshot(dir.path());QVERIFY(before.complete);
        QVERIFY(before.entries[fs_watch::key(a)].revision.identity().isEmpty());
        QVERIFY(QFile::rename(a,dir.filePath("c.png")));
        QVERIFY(fs_watch::renames(before,fs_watch::snapshot(dir.path())).isEmpty());
    }
    void boundedIncompleteAndCancelledSnapshots() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        for(int i=0;i<5;++i)QVERIFY(write(dir.filePath(QString::number(i)+".png"),"generated"));
        const auto complete=fs_watch::snapshot(dir.path());QVERIFY(complete.complete);QCOMPARE(complete.entries.size(),5);
        const auto limited=fs_watch::snapshot(dir.path(),{},2);QVERIFY(!limited.complete);QCOMPARE(limited.entries.size(),2);
        const auto byteLimited=fs_watch::snapshot(dir.path(),{},20000,128);QVERIFY(!byteLimited.complete);QVERIFY(byteLimited.entries.isEmpty());
        QVERIFY(fs_watch::renames(complete,limited).isEmpty());
        std::stop_source cancel;cancel.request_stop();
        const auto stopped=fs_watch::snapshot(dir.path(),cancel.get_token());QVERIFY(stopped.cancelled);QVERIFY(!stopped.complete);
        const auto absent=fs_watch::snapshot(dir.filePath("absent"));QVERIFY(absent.missing);QVERIFY(!absent.complete);
    }
    void ambiguousIdentitiesAndPartialSnapshots() {
        fs_watch::Snapshot a,b;a.complete=b.complete=true;
        fs_watch::Revision revision;revision.known=true;revision.volume=1;revision.object=2;revision.born=3;revision.links=1;
        a.entries.insert("a",{"a",revision});b.entries.insert("b",{"b",revision});
        QCOMPARE(fs_watch::renames(a,b).value("a"),QString("b"));
        b.entries.insert("c",{"c",revision});QVERIFY(fs_watch::renames(a,b).isEmpty());
        b.entries.remove("c");b.complete=false;QVERIFY(fs_watch::renames(a,b).isEmpty());
        b.complete=true;b.entries["b"].revision.born=4;QVERIFY(fs_watch::renames(a,b).isEmpty());
    }
};
QTEST_MAIN(FSWatchTests)
#include "fs_watch_tests.moc"
