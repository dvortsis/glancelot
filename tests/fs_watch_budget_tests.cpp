#include <QtTest>
#include <QProcess>
#include <QPointer>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include "fs_revision.hpp"
#include "fs_source_token.hpp"
#include "fs_monitor.hpp"
#include "io_job_lease.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/io_scheduler.hpp"
#include "glancelot/thumbnail_service.hpp"
#include "glancelot/metadata_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "file_grid_wiring.hpp"
#include <QSaveFile>
#include <QImage>
#include <QDateTime>
#include <QJsonDocument>
#include <QSharedMemory>
#include <QUuid>
#include <QBuffer>
#include <QDataStream>
#include <QScopeGuard>
#include <atomic>
#include <thread>

class FSWatchBudgetTests : public QObject {
    Q_OBJECT
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QByteArray hash(const QString& path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    }
    QTemporaryDir config;
    static bool png(const QString& path,Qt::GlobalColor color,QSize size=QSize(32,24)) {
        QSaveFile target(path);if(!target.open(QIODevice::WriteOnly)) {
            qWarning()<<"generated PNG open failed"<<target.error()<<target.errorString();return false;
        }
        QImage image(size,QImage::Format_RGB32);image.fill(color);
        if(!image.save(&target,"PNG")) {
            qWarning()<<"generated PNG encoding failed"<<target.error()<<target.errorString();return false;
        }
        if(!target.commit()) {
            qWarning()<<"generated PNG atomic commit failed"<<target.error()<<target.errorString();return false;
        }
        return true;
    }
private slots:
    void initTestCase(){QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());}
    void hundredThousandEntriesSustainedWriterKeepsGuiResponsive() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto image=dir.filePath("000000.png");
        QVERIFY(png(image,Qt::red));const auto original=hash(image);
        QVector<fs_watch::ModelPublicationStats> publications;
        const auto priorTiming=fs_watch::modelPublicationTiming;
        fs_watch::modelPublicationTiming=[&](const auto& value){publications.push_back(value);};
        const auto restoreTiming=qScopeGuard([&]{fs_watch::modelPublicationTiming=priorTiming;});
        ViewerController viewer;DirectoryEntryModel grid;file_grid_wiring::followViewer(viewer,grid);grid.setActive(true);
        const auto reportPublications=qScopeGuard([&] {
            qint64 maximum=-1;
            for(const auto& value:publications) {
                maximum=std::max(maximum,value.totalMs);
                if(value.rows==100000)qInfo()<<"100k publication ms: total"<<value.totalMs<<"identity"<<value.identityMs
                    <<"merge (includes disposal)"<<value.mergeMs<<"merge scan"<<value.mergeScanMs<<"reset"<<value.resetMs
                    <<"projection/raw mirror"<<value.projectionMs<<"selection"<<value.selectionMs
                    <<"observed/accepted identities"<<value.observedIdentities<<value.acceptedIdentities
                    <<"applied/filtered"<<value.applied<<value.filtered;
            }
            qInfo()<<"GUI publication callbacks"<<publications.size()<<"maximum"<<maximum<<"ms; loading at exit"<<grid.loading();
        });
        // Establish a real complete baseline, then exceed the identity budget.
        // Reuse this fixture for the admission and last-complete-baseline regression.
        viewer.open(image);QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&!grid.loading()&&grid.count()==1,10000);
        grid.select(0);QCOMPARE(grid.selectedPaths(),QStringList{image});
        const auto nativeIdentity=fs_watch::probe(fs_watch::native(image)).identity();
        QElapsedTimer setup;setup.start();
        for(int i=1;i<100000;++i)
            QVERIFY(write(dir.filePath(QString("%1.dat").arg(i,6,10,QChar('0'))),"generated"));
        const auto setupMs=setup.elapsed();
        publications.clear();
        QElapsedTimer clock;clock.start();qint64 previous=0,worst=0;
        QTimer heartbeat;heartbeat.setInterval(1);
        QObject::connect(&heartbeat,&QTimer::timeout,this,[&]{const auto now=clock.elapsed();worst=std::max(worst,now-previous);previous=now;});
        heartbeat.start();grid.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!grid.loading()&&grid.count()==100000&&viewer.imageReady(),30000);
        const auto initialGap=worst;worst=0;previous=clock.elapsed();
        QSignalSpy invalidations(&viewer,&ViewerController::filesInvalidated);
        QSignalSpy metadata(&grid,&QAbstractItemModel::dataChanged);
        std::atomic_int writes{};std::atomic_bool failed{};
        const auto destination=dir.filePath("099999.dat");
        // Real filesystem atomic replacements on a separate thread, never a
        // simulated notification or a GUI-thread write during measurement.
        std::jthread writer([&](std::stop_token stop){
            while(!stop.stop_requested()) {
                QSaveFile file(destination);const auto bytes=QByteArray::number(writes.load())+" generated changed bytes";
                if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){failed=true;return;}
                ++writes;std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        });
        QTest::qWait(8000);writer.request_stop();writer.join();
        QVERIFY(!failed.load());QVERIFY(writes.load()>20);
        QTRY_VERIFY_WITH_TIMEOUT(invalidations.count()>0,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!grid.loading()&&metadata.count()>0,30000);
        heartbeat.stop();worst=std::max(worst,clock.elapsed()-previous);
        qInfo()<<"100000 entries: generated setup"<<setupMs<<"ms; initial GUI gap"<<initialGap
               <<"ms; sustained-writer/recovery GUI gap"<<worst<<"ms; writes"<<writes.load()<<"invalidations"<<invalidations.count();
        qint64 maxPublication=-1;bool admittedWithoutPrefix=false;
        for(const auto& value:publications) {
            maxPublication=std::max(maxPublication,value.totalMs);
            if(value.applied&&value.rows==100000&&!value.complete&&value.observedIdentities==0&&value.acceptedIdentities==1)
                admittedWithoutPrefix=true;
        }
        QVERIFY2(admittedWithoutPrefix,"100k rows must publish without a partial identity prefix and retain the one-entry complete baseline");
        QVERIFY2(!publications.isEmpty()&&maxPublication<250,"actual model publication exceeded the unchanged 250ms GUI budget");
        QCOMPARE(grid.count(),100000);QVERIFY(viewer.imageReady());QCOMPARE(viewer.filePath(),image);
        QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::red));QCOMPARE(hash(image),original);
        QVERIFY2(initialGap<250,qPrintable(QString("100k initial publication GUI gap %1 ms").arg(initialGap)));
        QVERIFY2(worst<250,qPrintable(QString("100k sustained writer GUI gap %1 ms").arg(worst)));
        // Exercise the real active-filter projection at the same dense row limit.
        // "0" matches every generated name; it cannot hide publication costs.
        const auto firstFilteredPublication=publications.size();
        worst=0;previous=clock.elapsed();heartbeat.start();
        QElapsedTimer filterChange;filterChange.start();viewer.setFilter("0",true);
        const auto filterChangeMs=filterChange.elapsed();
        QCOMPARE(grid.count(),100000);QVERIFY(grid.hasFilter());
        std::atomic_int filteredWrites{};std::atomic_bool filteredFailure{};
        std::atomic<qint64> filteredSize{};
        std::jthread filteredWriter([&](std::stop_token stop){
            while(!stop.stop_requested()) {
                QSaveFile file(destination);
                const auto sequence=filteredWrites.load();
                // Unique bounded sizes make the final metadata oracle distinguish
                // the last atomic replacement from every earlier publication.
                if(sequence>=4096){filteredFailure=true;return;}
                const auto bytes=QByteArray(128+sequence,'x');
                if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){filteredFailure=true;return;}
                filteredSize=bytes.size();++filteredWrites;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        });
        QTest::qWait(2000);filteredWriter.request_stop();filteredWriter.join();
        QVERIFY(!filteredFailure.load());QVERIFY(filteredWrites.load()>20);
        QTRY_VERIFY_WITH_TIMEOUT(!grid.loading()&&grid.rowOf(destination)>=0&&
            grid.data(grid.index(grid.rowOf(destination)),DirectoryEntryModel::SizeRole).toLongLong()==filteredSize.load(),30000);
        heartbeat.stop();worst=std::max(worst,clock.elapsed()-previous);
        qint64 filteredMaximum=-1;bool filteredPublished=false;
        for(auto i=firstFilteredPublication;i<publications.size();++i) {
            const auto& value=publications[i];
            if(value.applied&&value.filtered&&value.rows==100000) {
                filteredPublished=true;filteredMaximum=std::max(filteredMaximum,value.totalMs);
            }
        }
        qInfo()<<"100k active filter: change"<<filterChangeMs<<"ms; GUI gap"<<worst
               <<"ms; publication"<<filteredMaximum<<"ms; writes"<<filteredWrites.load();
        QVERIFY(filteredPublished);
        QVERIFY2(filterChangeMs<250,"100k filter change exceeded the unchanged 250ms GUI budget");
        QVERIFY2(filteredMaximum<250,"100k filtered publication exceeded the unchanged 250ms GUI budget");
        QVERIFY2(worst<250,"100k filtered writer exceeded the unchanged 250ms GUI budget");
        viewer.clearFilter();
        // Return to a complete observation and rename the selected original.
        // Keeping a partial baseline instead of the original complete one loses it.
        for(int i=1;i<100000;++i)
            QVERIFY(QFile::remove(dir.filePath(QString("%1.dat").arg(i,6,10,QChar('0')))));
        const auto renamed=dir.filePath("renamed.png");QVERIFY(QFile::rename(image,renamed));grid.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!grid.loading()&&grid.count()==1&&grid.rowOf(renamed)>=0,10000);
        for(const auto& value:publications)
            QVERIFY2(value.totalMs<250,"model publication exceeded 250ms while recovering the complete baseline");
        if(nativeIdentity.isEmpty())QSKIP("No native identity: budget assertions ran, rename retention is unqualified");
        QCOMPARE(grid.selectedPaths(),QStringList{renamed});QCOMPARE(grid.cursorRow(),grid.rowOf(renamed));
        QCOMPARE(hash(renamed),original);
    }
};
QTEST_MAIN(FSWatchBudgetTests)
#include "fs_watch_budget_tests.moc"
