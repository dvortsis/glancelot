#include "glancelot/settings_recovery.hpp"
#include "file_lock.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>

using namespace glancelot::recovery;
class RecoveryTests final : public QObject {
    Q_OBJECT
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QByteArray read(const QString& path) {QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
private slots:
    void absentAndPresentEmptyHaveDifferentRevisions() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const Context context{dir.path()};
        const auto absent=readRaw(context);QCOMPARE(absent.status.code,Code::Ok);QVERIFY(absent.snapshot);
        QVERIFY(!absent.snapshot->primary.present);QCOMPARE(absent.snapshot->primary.kind,ParseKind::Absent);
        QVERIFY(write(dir.filePath("settings.json"),{}));
        const auto empty=readRaw(context);QCOMPARE(empty.status.code,Code::Ok);QVERIFY(empty.snapshot);
        QVERIFY(empty.snapshot->primary.present);QCOMPARE(empty.snapshot->primary.kind,ParseKind::Corrupt);
        QCOMPARE(empty.snapshot->primary.sha256,QCryptographicHash::hash(QByteArray{},QCryptographicHash::Sha256));
        QVERIFY(empty.snapshot->revision!=absent.snapshot->revision);
        // Independent wire vector: domain + absent member encodings, not a call
        // to the implementation's encoder or JSON serialization.
        QByteArray vector("Glancelot.SettingsPair");vector.append(char(0));vector.append(char(1));vector.append(QByteArray(82,char(0)));
        QCOMPARE(absent.snapshot->revision,QString("sr1:")+QString::fromLatin1(QCryptographicHash::hash(vector,QCryptographicHash::Sha256).toHex()));
    }
    void exactPairBytesSurviveClassification() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QByteArray bad("not JSON\0 retained",18),backup("{\n \"schema\":1,\"future\":{\"x\":true}}\n");
        QVERIFY(write(dir.filePath("settings.json"),bad));QVERIFY(write(dir.filePath("settings.json.bak"),backup));
        const auto result=readRaw({dir.path()});QCOMPARE(result.status.code,Code::Ok);QVERIFY(result.snapshot);
        QCOMPARE(result.snapshot->primary.kind,ParseKind::Corrupt);QCOMPARE(result.snapshot->primary.bytes,bad);
        QCOMPARE(result.snapshot->lastGood.kind,ParseKind::Supported);QCOMPARE(result.snapshot->lastGood.bytes,backup);
        QCOMPARE(read(dir.filePath("settings.json")),bad);QCOMPARE(read(dir.filePath("settings.json.bak")),backup);
        QVERIFY(!QFile::exists(dir.filePath("settings.json.damaged")));
    }
    void formattingAndBackupOnlyEditsInvalidateRevision() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto primary=dir.filePath("settings.json"),backup=dir.filePath("settings.json.bak");
        QVERIFY(write(primary,"{\"schema\":1,\"value\":2}"));const auto first=readRaw({dir.path()});QVERIFY(first.snapshot);
        QVERIFY(write(primary,"{ \"value\": 2, \"schema\": 1 }\n"));const auto formatted=readRaw({dir.path()});QVERIFY(formatted.snapshot);
        QVERIFY(first.snapshot->revision!=formatted.snapshot->revision);
        QVERIFY(write(backup,"{\"schema\":1}"));const auto added=readRaw({dir.path()});QVERIFY(added.snapshot);
        QCOMPARE(added.snapshot->primary.bytes,formatted.snapshot->primary.bytes);QVERIFY(added.snapshot->revision!=formatted.snapshot->revision);
        QVERIFY(write(backup,"{\"schema\":1,\"other\":true}"));const auto changed=readRaw({dir.path()});QVERIFY(changed.snapshot);
        QVERIFY(changed.snapshot->revision!=added.snapshot->revision);
    }
    void unsupportedSchemasRemainDiagnostic_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("future")<<QByteArray("{\"schema\":2,\"future\":true}");
        QTest::newRow("fractional")<<QByteArray("{\"schema\":1.5}");
        QTest::newRow("missing")<<QByteArray("{}");
        QTest::newRow("wrong-type")<<QByteArray("{\"schema\":\"1\"}");
    }
    void unsupportedSchemasRemainDiagnostic() {
        QFETCH(QByteArray,bytes);QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(write(dir.filePath("settings.json"),bytes));
        const auto result=readRaw({dir.path()});QCOMPARE(result.status.code,Code::Ok);QVERIFY(result.snapshot);
        QCOMPARE(result.snapshot->primary.kind,ParseKind::Unsupported);QCOMPARE(result.snapshot->primary.bytes,bytes);
        QCOMPARE(read(dir.filePath("settings.json")),bytes);
    }
    void oversizedMemberYieldsNoSnapshot_data() {
        QTest::addColumn<QString>("member");QTest::newRow("primary")<<QString("settings.json");QTest::newRow("backup")<<QString("settings.json.bak");
    }
    void oversizedMemberYieldsNoSnapshot() {
        QFETCH(QString,member);QTemporaryDir dir;QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath(member),QByteArray(MaxMemberBytes+1,'x')));
        const auto result=readRaw({dir.path()});QCOMPARE(result.status.code,Code::LimitExceeded);QVERIFY(!result.snapshot);
        QCOMPARE(QFileInfo(dir.filePath(member)).size(),MaxMemberBytes+1);
    }
    void directoryMemberIsNotAbsence() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(QDir(dir.path()).mkdir("settings.json"));
        const auto result=readRaw({dir.path()});QCOMPARE(result.status.code,Code::Unreadable);QVERIFY(!result.snapshot);
    }
    void maximumSizedMemberIsReadExactly() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QByteArray bytes("{\"schema\":1}");bytes.append(QByteArray(MaxMemberBytes-bytes.size(),' '));
        QVERIFY(write(dir.filePath("settings.json"),bytes));
        const auto result=readRaw({dir.path()});QCOMPARE(result.status.code,Code::Ok);QVERIFY(result.snapshot);
        QCOMPARE(result.snapshot->primary.kind,ParseKind::Supported);
        QCOMPARE(result.snapshot->primary.bytes,bytes);QCOMPARE(result.snapshot->primary.bytes.size(),MaxMemberBytes);
    }
    void namespaceIsSeparateFromRawRevision() {
        QTemporaryDir first,second;QVERIFY(first.isValid()&&second.isValid());
        const auto a=readRaw({first.path()}),b=readRaw({second.path()});QVERIFY(a.snapshot&&b.snapshot);
        QCOMPARE(a.snapshot->revision,b.snapshot->revision);QVERIFY(a.snapshot->namespaceId!=b.snapshot->namespaceId);
        const auto alias=readRaw({first.path()+"/child/.."});QVERIFY(alias.snapshot);
        QCOMPARE(a.snapshot->namespaceId,alias.snapshot->namespaceId);
    }
    void invalidRequestAndCancellationDoNotReadOrCreate() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto missing=dir.filePath("not-created");
        std::stop_source stopped;stopped.request_stop();Options cancelled;cancelled.cancellation=stopped.get_token();cancelled.generation=93;
        const auto result=readRaw({missing},cancelled);QCOMPARE(result.status.code,Code::Cancelled);QCOMPARE(result.status.generation,quint64(93));
        QVERIFY(!result.snapshot);QVERIFY(!QFile::exists(missing));
        Options invalid;invalid.lockTimeoutMs=MaxLockWaitMs+1;
        QCOMPARE(readRaw({missing},invalid).status.code,Code::LimitExceeded);QVERIFY(!QFile::exists(missing));
        const auto relative=readRaw({"relative-config"});QCOMPARE(relative.status.code,Code::Unreadable);QVERIFY(!relative.snapshot);
    }
    void contendedLockCanCancelWithoutTouchingMembers() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray original("{\"schema\":1}");
        QVERIFY(write(dir.filePath("settings.json"),original));FileLock held(dir.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
        Options immediate;immediate.lockTimeoutMs=0;const auto busy=readRaw({dir.path()},immediate);
        QCOMPARE(busy.status.code,Code::Busy);QVERIFY(!busy.snapshot);
        std::stop_source stop;Options options;options.cancellation=stop.get_token();
        SnapshotResult result;std::atomic_bool entered{};
        std::jthread worker([&]{entered=true;result=readRaw({dir.path()},options);});
        QTRY_VERIFY(entered.load());QTest::qWait(20);stop.request_stop();worker.join();
        QCOMPARE(result.status.code,Code::Cancelled);QVERIFY(!result.snapshot);
        QCOMPARE(read(dir.filePath("settings.json")),original);QVERIFY(!QFile::exists(dir.filePath("settings.json.bak")));
    }
};
QTEST_GUILESS_MAIN(RecoveryTests)
#include "recovery_tests.moc"
