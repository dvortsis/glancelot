#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include "fs_revision.hpp"
#include "fs_monitor.hpp"

class FSWatchTests : public QObject {
    Q_OBJECT
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QByteArray hash(const QString& path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
        return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);
    }
private slots:
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
QTEST_GUILESS_MAIN(FSWatchTests)
#include "fs_watch_tests.moc"
