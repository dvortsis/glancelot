#include "glancelot/settings_recovery.hpp"
#include "glancelot/recovery_checkpoint.hpp"
#include "settings_maintenance.hpp"
#include "recovery_session.hpp"
#include "file_lock.hpp"
#include "settings_transaction.hpp"
#include "settings_package.hpp"
#include "settings_policy.hpp"
#include "settings_diff.hpp"
#include "window_claim_guard.hpp"
#include "glancelot/window_slot.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include "glancelot/settings_file.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <mutex>

using namespace glancelot::recovery;
class RecoveryTests final : public QObject {
    Q_OBJECT
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QByteArray read(const QString& path) {QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
    struct Hooks {
        const glancelot::settings_detail::TransactionHooks* old=glancelot::settings_detail::transactionHooks;
        explicit Hooks(const glancelot::settings_detail::TransactionHooks& hooks){glancelot::settings_detail::transactionHooks=&hooks;}
        ~Hooks(){glancelot::settings_detail::transactionHooks=old;}
    };
    static glancelot::settings_detail::Request restoreRequest(const RawSnapshot& snapshot) {
        glancelot::settings_detail::Request request;
        request.context.directory=snapshot.directory;request.expectedNamespace=snapshot.namespaceId;
        request.expectedRevision=snapshot.revision;request.policy=glancelot::settings_detail::Policy::Restore;
        request.options.lockTimeoutMs=0;return request;
    }
private slots:
    void interruptedRestoreProcessPreservesAtomicState_data() {
        using glancelot::settings_detail::Stage;
        QTest::addColumn<int>("point");
        QTest::newRow("before-evidence")<<int(Stage::BeforeEvidence);
        QTest::newRow("after-sealed-preimage")<<int(Stage::AfterPreimage);
        QTest::newRow("before-primary-replacement")<<int(Stage::BeforeReplace);
        QTest::newRow("after-primary-replacement")<<int(Stage::AfterReplace);
    }
    void interruptedRestoreProcessPreservesAtomicState() {
        using glancelot::settings_detail::Stage;
        QFETCH(int,point);
        QTemporaryDir target,source,packages;
        QVERIFY(target.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray primary("{ \"schema\":1,\"wheel\":\"zoom\",\"unknown\":{\"future\":5},\"windows\":{\"2\":{\"image\":\"retain\"}} }");
        const QByteArray backup("exact diagnostic backup"),damaged("exact damaged evidence");
        QVERIFY(write(target.filePath("settings.json"),primary));
        QVERIFY(write(target.filePath("settings.json.bak"),backup));
        QVERIFY(write(target.filePath("settings.json.damaged"),damaged));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\"}"));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));
        QCOMPARE(captured.status.code,Code::Ok);
        QProcess child;
        const auto cleanup=qScopeGuard([&]{if(child.state()!=QProcess::NotRunning){child.kill();child.waitForFinished(5000);}});
        child.start(QCoreApplication::applicationFilePath(),{"--restore-crash-helper",target.path(),captured.destination,QString::number(point)});
        QVERIFY2(child.waitForStarted(5000),qPrintable(child.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(child.canReadLine()||child.state()==QProcess::NotRunning,10000);
        const auto message=child.readLine();
        QVERIFY2(message.trimmed()==QByteArray("restore-stage-ready"),child.readAllStandardError().constData());
        QVERIFY(child.state()!=QProcess::NotRunning);
        child.kill();
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitStatus(),QProcess::CrashExit);
        // A killed writer must release its OS lock without deleting lock files.
        const auto actual=readRaw({target.path()});
        QCOMPARE(actual.status.code,Code::Ok);QVERIFY(actual.snapshot);
        QCOMPARE(actual.snapshot->lastGood.bytes,backup);
        QCOMPARE(read(target.filePath("settings.json.damaged")),damaged);
        if(point==int(Stage::AfterReplace)) {
            auto expected=QJsonDocument::fromJson(primary).object();expected["wheel"]="pan";
            QCOMPARE(QJsonDocument::fromJson(actual.snapshot->primary.bytes).object(),expected);
        }else QCOMPARE(actual.snapshot->primary.bytes,primary);
        const QDir preimages(target.filePath("recovery-preimages"));
        const auto names=preimages.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        if(point==int(Stage::BeforeEvidence))QVERIFY(names.isEmpty());
        else {
            QCOMPARE(names.size(),1);
            const auto sealed=package::read(preimages.filePath(names.first()));
            QCOMPARE(sealed.status.code,Code::Ok);QVERIFY(sealed.data);
            QCOMPARE(sealed.data->primary.bytes,primary);QCOMPARE(sealed.data->lastGood.bytes,backup);
        }
    }
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
    void ordinaryWriterStillMergesAndKeepsExactLastGood() {
        QTemporaryDir dir;QVERIFY(dir.isValid());qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
        const QByteArray original("{ \"schema\":1, \"unknown\":{\"future\":true},\"boundary\":\"stop\" }");
        QVERIFY(write(dir.filePath("settings.json"),original));
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["wheel"]="zoom";}).ok());
        QCOMPARE(read(dir.filePath("settings.json.bak")),original);
        QJsonObject result;QVERIFY(glancelot::readSettings(result).ok());
        QCOMPARE(result.value("wheel").toString(),QString("zoom"));QCOMPARE(result.value("boundary").toString(),QString("stop"));
        QVERIFY(result.value("unknown").toObject().value("future").toBool());
    }
    void ordinaryWriterRecoversCorruptButRefusesFuture() {
        QTemporaryDir dir;QVERIFY(dir.isValid());qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
        const QByteArray damaged("broken bytes"),backup("{\"schema\":1,\"unknown\":7}");
        QVERIFY(write(dir.filePath("settings.json"),damaged));QVERIFY(write(dir.filePath("settings.json.bak"),backup));
        const auto saved=glancelot::updateSettings([](QJsonObject& root){root["wheel"]="pan";});
        QVERIFY(saved.ok());QVERIFY(saved.recovered);QCOMPARE(read(dir.filePath("settings.json.damaged")),damaged);
        QCOMPARE(read(dir.filePath("settings.json.bak")),backup);
        const QByteArray future("{\"schema\":3}");QVERIFY(write(dir.filePath("settings.json"),future));
        bool called=false;const auto refused=glancelot::updateSettings([&](QJsonObject&){called=true;});
        QCOMPARE(refused.code,glancelot::SettingsStatus::Unsupported);QVERIFY(!called);
        QCOMPARE(read(dir.filePath("settings.json")),future);QCOMPARE(read(dir.filePath("settings.json.bak")),backup);
    }
    void ordinaryBackupFailureAndOversizeLeavePrimaryUntouched() {
        QTemporaryDir dir;QVERIFY(dir.isValid());qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
        const QByteArray primary("{\"schema\":1,\"unknown\":true}");QVERIFY(write(dir.filePath("settings.json"),primary));
        QVERIFY(QDir(dir.path()).mkdir("settings.json.bak"));
        const auto failed=glancelot::updateSettings([](QJsonObject& root){root["wheel"]="zoom";});
        QCOMPARE(failed.code,glancelot::SettingsStatus::WriteFailed);QCOMPARE(read(dir.filePath("settings.json")),primary);
        QVERIFY(QDir(dir.path()).rmdir("settings.json.bak"));
        const auto large=glancelot::updateSettings([](QJsonObject& root){root["oversized"]=QString(int(MaxMemberBytes),'x');});
        QCOMPARE(large.code,glancelot::SettingsStatus::WriteFailed);QCOMPARE(read(dir.filePath("settings.json")),primary);
        QVERIFY(!QFile::exists(dir.filePath("settings.json.bak")));
    }
    void restoreMustHaveRawGuardsAndPreimageWriter() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray primary("{\"schema\":1}");
        QVERIFY(write(dir.filePath("settings.json"),primary));const auto snapshot=readRaw({dir.path()});QVERIFY(snapshot.snapshot);
        auto request=restoreRequest(*snapshot.snapshot);bool edited=false;
        auto editor=[&](const View& view){edited=true;return Edit{Code::Committed,view.base};};
        QCOMPARE(transact(request,editor).status.code,Code::Corrupt);QVERIFY(!edited);
        request.expectedRevision.clear();
        QCOMPARE(transact(request,editor,[](const RawSnapshot&){return Seal{{Code::Ok},"pretend"};}).status.code,Code::Corrupt);
        QVERIFY(!edited);QCOMPARE(read(dir.filePath("settings.json")),primary);
    }
    void failedPreimageNeverReplacesPrimary() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray primary("{\"schema\":1}");QVERIFY(write(dir.filePath("settings.json"),primary));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);
        const auto result=transact(restoreRequest(*before.snapshot),[](const View& view){auto changed=view.base;changed["wheel"]="zoom";return Edit{Code::Committed,changed};},
            [](const RawSnapshot&){return Seal{{Code::WriteFailed},QString{}};});
        QCOMPARE(result.status.code,Code::WriteFailed);QVERIFY(result.preimage.isEmpty());
        QCOMPARE(read(dir.filePath("settings.json")),primary);QVERIFY(!QFile::exists(dir.filePath("settings.json.bak")));
    }
    void preimageCannotClaimPrimaryWasAlreadyCommitted() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray primary("{\"schema\":1}");QVERIFY(write(dir.filePath("settings.json"),primary));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);
        const auto result=transact(restoreRequest(*before.snapshot),[](const View& view){auto changed=view.base;changed["wheel"]="zoom";return Edit{Code::Committed,changed};},
            [](const RawSnapshot&){return Seal{{Code::Committed},QString{}};});
        QCOMPARE(result.status.code,Code::WriteFailed);QCOMPARE(read(dir.filePath("settings.json")),primary);
    }
    void staleRestoreConflictsBeforeNoChangeOrPreimage() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(write(dir.filePath("settings.json"),"{\"schema\":1}"));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);auto request=restoreRequest(*before.snapshot);
        QVERIFY(write(dir.filePath("settings.json.bak"),"{\"schema\":1,\"changed\":true}"));
        bool edited=false,sealed=false;
        const auto result=transact(request,[&](const View&){edited=true;return Edit{};},[&](const RawSnapshot&){sealed=true;return Seal{};});
        QCOMPARE(result.status.code,Code::Conflict);QVERIFY(!edited);QVERIFY(!sealed);QVERIFY(result.preimage.isEmpty());
    }
    void checkedNoChangeDoesNotRewriteEvidence() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray primary("{ \"schema\":1 }"),backup("old diagnostic bytes"),damaged("keep damaged");
        QVERIFY(write(dir.filePath("settings.json"),primary));QVERIFY(write(dir.filePath("settings.json.bak"),backup));QVERIFY(write(dir.filePath("settings.json.damaged"),damaged));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);bool sealed=false;
        const auto result=transact(restoreRequest(*before.snapshot),[](const View&){return Edit{};},[&](const RawSnapshot&){sealed=true;return Seal{};});
        QCOMPARE(result.status.code,Code::NoChange);QVERIFY(!sealed);QCOMPARE(result.revision,before.snapshot->revision);
        QCOMPARE(read(dir.filePath("settings.json")),primary);QCOMPARE(read(dir.filePath("settings.json.bak")),backup);QCOMPARE(read(dir.filePath("settings.json.damaged")),damaged);
    }
    void restoreFailureAndCancelRetainOriginals_data() {
        QTest::addColumn<int>("point");QTest::addColumn<bool>("cancel");
        for(const auto stage:{glancelot::settings_detail::Stage::AfterRead,glancelot::settings_detail::Stage::BeforeEvidence,
                             glancelot::settings_detail::Stage::AfterPreimage,glancelot::settings_detail::Stage::BeforeReplace}) {
            const auto label=QByteArray::number(int(stage));
            QTest::newRow((label+"-failure").constData())<<int(stage)<<false;
            QTest::newRow((label+"-cancel").constData())<<int(stage)<<true;
        }
    }
    void restoreFailureAndCancelRetainOriginals() {
        using namespace glancelot::settings_detail;QFETCH(int,point);QFETCH(bool,cancel);
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray primary("{\"schema\":1,\"unknown\":4}"),backup("{\"schema\":1}"),damaged("existing damaged");
        QVERIFY(write(dir.filePath("settings.json"),primary));QVERIFY(write(dir.filePath("settings.json.bak"),backup));QVERIFY(write(dir.filePath("settings.json.damaged"),damaged));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);auto request=restoreRequest(*before.snapshot);
        std::stop_source stop;request.options.cancellation=stop.get_token();TransactionHooks hooks;
        hooks.observe=[&](Stage stage){if(cancel&&int(stage)==point)stop.request_stop();};
        hooks.fail=[&](Stage stage){return !cancel&&int(stage)==point;};Hooks installed(hooks);
        const auto preimage=dir.filePath("generated-preimage");
        const auto result=transact(request,[](const View& view){auto changed=view.base;changed["wheel"]="zoom";return Edit{Code::Committed,changed};},
            [&](const RawSnapshot& raw){const bool stored=write(preimage,raw.primary.bytes);return Seal{{stored?Code::Ok:Code::WriteFailed},stored?preimage:QString{}};});
        QCOMPARE(result.status.code,cancel?Code::Cancelled:Code::WriteFailed);
        QCOMPARE(read(dir.filePath("settings.json")),primary);QCOMPARE(read(dir.filePath("settings.json.bak")),backup);QCOMPARE(read(dir.filePath("settings.json.damaged")),damaged);
        if(point>=int(Stage::AfterPreimage)){QCOMPARE(result.preimage,preimage);QCOMPARE(read(preimage),primary);}
        else {QVERIFY(result.preimage.isEmpty());QVERIFY(!QFile::exists(preimage));}
    }
    void cancelAfterReplacementIsCommitted() {
        using namespace glancelot::settings_detail;
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(write(dir.filePath("settings.json"),"{\"schema\":1}"));
        const auto before=readRaw({dir.path()});QVERIFY(before.snapshot);auto request=restoreRequest(*before.snapshot);
        std::stop_source stop;request.options.cancellation=stop.get_token();TransactionHooks hooks;
        hooks.observe=[&](Stage stage){if(stage==Stage::AfterReplace)stop.request_stop();};Hooks installed(hooks);
        const auto result=transact(request,[](const View& view){auto changed=view.base;changed["wheel"]="zoom";return Edit{Code::Committed,changed};},
            [&](const RawSnapshot& snapshot){const auto path=dir.filePath("preimage");const bool ok=write(path,snapshot.primary.bytes);return Seal{{ok?Code::Ok:Code::WriteFailed},ok?path:QString{}};});
        QCOMPARE(result.status.code,Code::Committed);QVERIFY(stop.stop_requested());QVERIFY(result.revision!=before.snapshot->revision);
        const auto current=readRaw({dir.path()});QVERIFY(current.snapshot);QCOMPARE(result.revision,current.snapshot->revision);
        QVERIFY(read(dir.filePath("settings.json")).contains("zoom"));QVERIFY(!QFile::exists(dir.filePath("settings.json.bak")));
    }
    void captureRetainsExactDiagnosticMembersAndSealedRevision() {
        QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());
        const QByteArray primary("corrupt retained bytes"),backup("{ \"schema\":2,\"future\":true }");
        QVERIFY(write(source.filePath("settings.json"),primary));QVERIFY(write(source.filePath("settings.json.bak"),backup));
        const auto captured=captureBackup({source.path()},target.filePath("capture"));QCOMPARE(captured.status.code,Code::Ok);
        const auto loaded=package::read(captured.destination);QCOMPARE(loaded.status.code,Code::Ok);QVERIFY(loaded.data);
        QCOMPARE(loaded.data->primary.bytes,primary);QCOMPARE(loaded.data->primary.kind,ParseKind::Corrupt);
        QCOMPARE(loaded.data->lastGood.bytes,backup);QCOMPARE(loaded.data->lastGood.kind,ParseKind::Unsupported);
        QCOMPARE(loaded.data->revision,captured.packageRevision);QVERIFY(captured.packageRevision.startsWith("sp1:"));
        QCOMPARE(read(source.filePath("settings.json")),primary);QCOMPARE(read(source.filePath("settings.json.bak")),backup);
        QVERIFY(!QFile::exists(source.filePath("settings.json.damaged")));
        QVERIFY(!QFile::exists(QDir(captured.destination).filePath(".manifest-seal")));
    }
    void captureAbsentAndEmptyRemainDistinct() {
        QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());
        const auto absent=captureBackup({source.path()},target.filePath("absent"));QCOMPARE(absent.status.code,Code::Ok);
        const auto first=package::read(absent.destination);QVERIFY(first.data);QVERIFY(!first.data->primary.present);
        QVERIFY(write(source.filePath("settings.json"),{}));
        const auto empty=captureBackup({source.path()},target.filePath("empty"));QCOMPARE(empty.status.code,Code::Ok);
        const auto second=package::read(empty.destination);QVERIFY(second.data);QVERIFY(second.data->primary.present);
        QVERIFY(first.data->revision!=second.data->revision);
        const auto collision=captureBackup({source.path()},target.filePath("absent"));QCOMPARE(collision.status.code,Code::Conflict);
        const auto unchanged=package::read(absent.destination);QVERIFY(unchanged.data);QCOMPARE(unchanged.data->revision,absent.packageRevision);
    }
    void destinationWorkDoesNotHoldSettingsLock() {
        QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());
        const QByteArray before("{\"schema\":1,\"version\":\"captured\"}"),after("{\"schema\":1,\"version\":\"newer\"}");
        QVERIFY(write(source.filePath("settings.json"),before));bool acquired=false,updated=false;
        package::Hooks hooks;hooks.observe=[&](package::Stage stage){
            if(stage!=package::Stage::DirectoryCreated)return;
            FileLock lock(source.filePath("settings.json.lock"));acquired=lock.acquire(0);
            if(acquired)updated=write(source.filePath("settings.json"),after);
        };
        const auto old=package::hooks;package::hooks=&hooks;const auto restore=qScopeGuard([&]{package::hooks=old;});
        const auto captured=captureBackup({source.path()},target.filePath("capture"));QCOMPARE(captured.status.code,Code::Ok);
        QVERIFY(acquired);QVERIFY(updated);const auto loaded=package::read(captured.destination);QVERIFY(loaded.data);
        QCOMPARE(loaded.data->primary.bytes,before);QCOMPARE(read(source.filePath("settings.json")),after);
    }
    void packageTamperingRefuses_data() {
        QTest::addColumn<QString>("mutation");
        for(const auto* name:{"bytes","missing","extra","unsealed","future","path","duplicate","length","hash","undeclared","manifest-overflow"})
            QTest::newRow(name)<<QString::fromLatin1(name);
    }
    void packageTamperingRefuses() {
        QFETCH(QString,mutation);QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1}"));
        const auto captured=captureBackup({source.path()},target.filePath("capture"));QCOMPARE(captured.status.code,Code::Ok);
        const QDir dir(captured.destination);const auto manifestPath=dir.filePath("manifest.json");
        auto manifest=QJsonDocument::fromJson(read(manifestPath)).object();auto members=manifest.value("members").toArray();
        if(mutation=="bytes")QVERIFY(write(dir.filePath("settings.json"),"changed"));
        else if(mutation=="missing")QVERIFY(QFile::remove(dir.filePath("settings.json")));
        else if(mutation=="extra")QVERIFY(write(dir.filePath("surprise"),"unexpected"));
        else if(mutation=="undeclared")QVERIFY(write(dir.filePath("settings.json.bak"),"unexpected backup"));
        else if(mutation=="manifest-overflow")QVERIFY(write(manifestPath,QByteArray(MaxManifestBytes+1,' ')));
        else {
            if(mutation=="unsealed")manifest["sealed"]=false;
            else if(mutation=="future")manifest["schema"]=2;
            else if(mutation=="duplicate")members[1]=members[0];
            else {
                auto member=members[0].toObject();
                if(mutation=="path")member["name"]="../settings.json";
                if(mutation=="length")member["bytes"]=0.5;
                if(mutation=="hash")member["sha256"]=QString(64,'f');
                members[0]=member;
            }
            manifest["members"]=members;QVERIFY(write(manifestPath,QJsonDocument(manifest).toJson(QJsonDocument::Compact)));
        }
        const auto loaded=package::read(dir.path());QVERIFY(!loaded.data);
        QCOMPARE(loaded.status.code,mutation=="future"?Code::Unsupported:mutation=="manifest-overflow"?Code::LimitExceeded:Code::Corrupt);
        QCOMPARE(read(source.filePath("settings.json")),QByteArray("{\"schema\":1}"));
    }
    void packageCancellationAtSeal_data() {
        QTest::addColumn<int>("point");
        for(const auto stage:{package::Stage::DirectoryCreated,package::Stage::PrimaryWritten,package::Stage::BackupWritten,package::Stage::BeforeSeal,package::Stage::AfterSeal})
            QTest::newRow(QByteArray::number(int(stage)).constData())<<int(stage);
    }
    void packageCancellationAtSeal() {
        QFETCH(int,point);QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1}"));
        std::stop_source stop;Options options;options.cancellation=stop.get_token();options.generation=51;
        package::Hooks hooks;hooks.observe=[&](package::Stage stage){if(int(stage)==point)stop.request_stop();};
        const auto old=package::hooks;package::hooks=&hooks;const auto restore=qScopeGuard([&]{package::hooks=old;});
        const auto captured=captureBackup({source.path()},target.filePath("capture"),options);
        QCOMPARE(captured.status.generation,quint64(51));
        const auto loaded=package::read(target.filePath("capture"));
        if(point==int(package::Stage::AfterSeal)) {QCOMPARE(captured.status.code,Code::Ok);QVERIFY(loaded.data);QCOMPARE(loaded.data->revision,captured.packageRevision);}
        else {QCOMPARE(captured.status.code,Code::Cancelled);QVERIFY(!loaded.data);QVERIFY(captured.packageRevision.isEmpty());}
    }
    void failedPackageWriteLeavesNoApplicableSeal() {
        QTemporaryDir source,target;QVERIFY(source.isValid()&&target.isValid());QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1}"));
        package::Hooks hooks;hooks.fail=[](package::Stage stage){return stage==package::Stage::BeforeSeal;};
        const auto old=package::hooks;package::hooks=&hooks;const auto restore=qScopeGuard([&]{package::hooks=old;});
        const auto captured=captureBackup({source.path()},target.filePath("capture"));QCOMPARE(captured.status.code,Code::WriteFailed);
        const auto loaded=package::read(captured.destination);QVERIFY(!loaded.data);QVERIFY(!QFile::exists(QDir(captured.destination).filePath("manifest.json")));
    }
    void globalPolicyPreservesUnknownAndPerSlotData() {
        const QJsonObject current{{"schema",1},{"unknown",QJsonObject{{"next",5}}},{"boundary","stop"},
            {"sort",QJsonObject{{"key","modified"},{"future",17}}},{"appearance",QJsonObject{{"mode","dark"},{"future",false}}},
            {"bindings",QJsonObject{{"future-command",QJsonObject{{"v",2}}},{"next","N"}}},
            {"windows",QJsonObject{{"1",QJsonObject{{"image","keep"}}}}},{"layouts",QJsonObject{{"9",QJsonObject{{"version",99}}}}},
            {"recovery",QJsonObject{{"version",3}}}};
        const QJsonObject source{{"schema",1},{"boundary","loop"},{"sort",QJsonObject{{"key","size"},{"newSourceUnknown",true}}},
            {"appearance",QJsonObject{{"family","slate-ice"},{"mode","light"}}},{"bindings",QJsonObject{{"next","J"},{"future-command","Escape"}}},
            {"storageProfile","hdd"},{"windows",QJsonObject{{"1",QJsonObject{{"image","overwrite"}}}}}};
        const auto merged=policy::merge(current,source,{});QCOMPARE(merged.status.code,Code::Ok);QVERIFY(merged.replacement);
        const auto result=*merged.replacement;
        QCOMPARE(result.value("boundary").toString(),QString("loop"));QCOMPARE(result.value("storageProfile").toString(),QString("hdd"));
        QCOMPARE(result.value("unknown"),current.value("unknown"));QCOMPARE(result.value("windows"),current.value("windows"));
        QCOMPARE(result.value("layouts"),current.value("layouts"));QCOMPARE(result.value("recovery"),current.value("recovery"));
        QCOMPARE(result.value("sort").toObject().value("future").toInt(),17);QVERIFY(!result.value("sort").toObject().contains("newSourceUnknown"));
        QCOMPARE(result.value("bindings").toObject().value("future-command"),current.value("bindings").toObject().value("future-command"));
        QCOMPARE(result.value("bindings").toObject().value("next").toString(),QString("J"));
    }
    void missingSelectedKnownValuesResetOnlyTheirKeys() {
        const QJsonObject current{{"schema",1},{"wheel","zoom"},{"boundary","stop"},{"textScale",1.5},
            {"sort",QJsonObject{{"key","modified"},{"future",true}}}};
        policy::Selection selection;selection.global=GlobalSection::Browsing;
        const auto result=policy::merge(current,{{"schema",1}},selection);QVERIFY(result.replacement);
        QCOMPARE(result.replacement->value("wheel"),current.value("wheel"));QCOMPARE(result.replacement->value("textScale"),current.value("textScale"));
        QVERIFY(!result.replacement->value("sort").toObject().contains("key"));QVERIFY(result.replacement->value("sort").toObject().value("future").toBool());
    }
    void selectedInvalidValuesRejectWholeMerge_data() {
        QTest::addColumn<QJsonObject>("source");
        QTest::newRow("bad-boundary")<<QJsonObject{{"schema",1},{"boundary","surprise"}};
        QTest::newRow("wrong-bool")<<QJsonObject{{"schema",1},{"filmstrip",1}};
        QTest::newRow("fractional-seed")<<QJsonObject{{"schema",1},{"sort",QJsonObject{{"seed",1.5}}}};
        QTest::newRow("oversize-scale")<<QJsonObject{{"schema",1},{"textScale",3}};
        QTest::newRow("unknown-family")<<QJsonObject{{"schema",1},{"appearance",QJsonObject{{"family","not-shipped"}}}};
        QTest::newRow("bad-profile")<<QJsonObject{{"schema",1},{"storageProfile","magnetic-tape"}};
        QTest::newRow("escape")<<QJsonObject{{"schema",1},{"bindings",QJsonObject{{"next","Escape"}}}};
        QTest::newRow("wrong-binding-type")<<QJsonObject{{"schema",1},{"bindings",QJsonObject{{"next",true}}}};
        QTest::newRow("prefix-conflict")<<QJsonObject{{"schema",1},{"bindings",QJsonObject{{"next","Ctrl+K"},{"previous","Ctrl+K, Ctrl+C"}}}};
    }
    void selectedInvalidValuesRejectWholeMerge() {
        QFETCH(QJsonObject,source);const QJsonObject current{{"schema",1},{"unknown",true}};
        const auto result=policy::merge(current,source,{});QCOMPARE(result.status.code,Code::Corrupt);QVERIFY(!result.replacement);QVERIFY(!result.invalidPath.isEmpty());
        QCOMPARE(current.value("unknown"),QJsonValue(true));
    }
    void unrelatedMalformedSectionsAreUntouched() {
        const QJsonObject current{{"schema",1},{"appearance","invalid retained object"},{"bindings",5},{"unknown",true}};
        const QJsonObject source{{"schema",1},{"wheel","pan"},{"appearance",QJsonObject{{"family","unknown"}}},{"sort",false}};
        policy::Selection selection;selection.global=GlobalSection::Viewing;
        const auto result=policy::merge(current,source,selection);QVERIFY(result.replacement);
        QCOMPARE(result.replacement->value("appearance"),current.value("appearance"));QCOMPARE(result.replacement->value("bindings"),current.value("bindings"));
        QCOMPARE(result.replacement->value("wheel"),QJsonValue("pan"));
        selection.global=GlobalSection::Appearance;
        const auto refused=policy::merge(current,source,selection);QCOMPARE(refused.status.code,Code::Corrupt);QVERIFY(!refused.replacement);
    }
    void positionPolicyTouchesOnlySelectedKnownSlotFields() {
        const QJsonObject current{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder","C:/old"},{"image","C:/old/a.png"},{"future",7}}},
            {"2",QJsonObject{{"future",true},{"image","retain"}}}}}};
#ifdef Q_OS_WIN
        const QString folder="C:/new",image="C:/new/b.png";
#else
        const QString folder="/new",image="/new/b.png";
#endif
        const QJsonObject source{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",image},{"saved",1},{"newUnknown",false}}}}}};
        policy::Selection selection;selection.slotSection=SlotSection::Position;selection.slot=1;
        const auto result=policy::merge(current,source,selection);QVERIFY(result.replacement);
        const auto windows=result.replacement->value("windows").toObject();QCOMPARE(windows.value("2"),current.value("windows").toObject().value("2"));
        QCOMPARE(windows.value("1").toObject().value("future").toInt(),7);QVERIFY(!windows.value("1").toObject().contains("newUnknown"));
        QCOMPARE(windows.value("1").toObject().value("image").toString(),image);
        auto bad=source;bad["windows"]=QJsonObject{{"1",QJsonObject{{"image","relative.png"}}}};
        const auto refused=policy::merge(current,bad,selection);QVERIFY(!refused.replacement);QCOMPARE(refused.status.code,Code::Corrupt);
    }
    void layoutPolicyPreservesUnknownPanesAndFutureSlots() {
        const QJsonObject current{{"schema",1},{"layouts",QJsonObject{{"1",QJsonObject{{"version",1},{"future",7},
            {"center",QJsonObject{{"ratio",0.5},{"future",true}}},{"panes",QJsonObject{{"tree",QJsonObject{{"extent",280},{"future",4}}},{"future-pane",QJsonObject{{"v",7}}}}}}},
            {"2",QJsonObject{{"version",99},{"future",true}}}}}};
        const QJsonObject source{{"schema",1},{"layouts",QJsonObject{{"1",QJsonObject{{"version",1},{"locked",true},
            {"center",QJsonObject{{"ratio",0.6},{"orientation","vertical"},{"mode","combined"}}},
            {"panes",QJsonObject{{"tree",QJsonObject{{"extent",320},{"region","left"},{"order",0},{"visible",false},{"frozen",true}}}}}}}}}};
        policy::Selection selection;selection.slotSection=SlotSection::Layout;selection.slot=1;
        const auto result=policy::merge(current,source,selection);QCOMPARE(result.status.code,Code::Ok);QVERIFY(result.replacement);
        const auto layouts=result.replacement->value("layouts").toObject();QCOMPARE(layouts.value("2"),current.value("layouts").toObject().value("2"));
        const auto slot=layouts.value("1").toObject();QCOMPARE(slot.value("future").toInt(),7);QVERIFY(slot.value("center").toObject().value("future").toBool());
        const auto panes=slot.value("panes").toObject();QCOMPARE(panes.value("tree").toObject().value("future").toInt(),4);
        QCOMPARE(panes.value("tree").toObject().value("extent").toInt(),320);QVERIFY(panes.contains("future-pane"));
        selection.slot=2;const auto refused=policy::merge(current,source,selection);QCOMPARE(refused.status.code,Code::Unsupported);QVERIFY(!refused.replacement);
    }
    void policyRevisionIsStableAndCoversShippedData() {
        const auto revision=policy::revision();QVERIFY(revision.startsWith("policy1:"));QCOMPARE(revision.size(),qsizetype(72));QCOMPARE(revision,policy::revision());
    }
    void diffSeparatesAppliedAndPreservedSourceChanges() {
        const QJsonObject current{{"schema",1},{"wheel","zoom"},{"unknown",QJsonObject{{"keep",true},{"remove",4}}}};
        const QJsonObject source{{"schema",1},{"wheel","pan"},{"unknown",QJsonObject{{"keep",true},{"added",8}}}};
        policy::Selection selection;selection.global=GlobalSection::Viewing;
        const auto merged=policy::merge(current,source,selection);QVERIFY(merged.replacement);
        const auto report=diff::build(current,source,*merged.replacement,merged.ownedPaths);
        QCOMPARE(report.status.code,Code::Ok);QVERIFY(!report.truncated);QCOMPARE(report.rows.size(),qsizetype(3));
        int applied=0,preserved=0;
        for(const auto& row:report.rows) {
            if(row.path==QStringList{"wheel"}){QCOMPARE(row.treatment,Treatment::Apply);QCOMPARE(row.before,QJsonValue("zoom"));QCOMPARE(row.after,QJsonValue("pan"));++applied;}
            else {QCOMPARE(row.treatment,Treatment::Preserve);QCOMPARE(row.path.first(),QString("unknown"));++preserved;}
        }
        QCOMPARE(applied,1);QCOMPARE(preserved,2);QCOMPARE(merged.replacement->value("unknown"),current.value("unknown"));
    }
    void diffDistinguishesMissingNullAndEmpty() {
        const QJsonObject current{{"null-to-absent",QJsonValue(QJsonValue::Null)},{"empty-to-null",""},{"empty-object",QJsonObject{}}};
        const QJsonObject source{{"absent-to-null",QJsonValue(QJsonValue::Null)},{"empty-to-null",QJsonValue(QJsonValue::Null)}};
        const auto report=diff::build(current,source,current,{});QCOMPARE(report.status.code,Code::Ok);QCOMPARE(report.rows.size(),qsizetype(4));
        for(const auto& row:report.rows) {
            if(row.path==QStringList{"absent-to-null"}){QVERIFY(!row.hadBefore);QVERIFY(row.hasAfter);QVERIFY(row.after.isNull());QCOMPARE(row.change,Change::Added);}
            if(row.path==QStringList{"null-to-absent"}){QVERIFY(row.hadBefore);QVERIFY(!row.hasAfter);QVERIFY(row.before.isNull());QCOMPARE(row.change,Change::Removed);}
            if(row.path==QStringList{"empty-to-null"}){QVERIFY(row.hadBefore&&row.hasAfter);QVERIFY(row.before.isString());QVERIFY(row.after.isNull());}
            if(row.path==QStringList{"empty-object"}){QVERIFY(row.before.isObject());QVERIFY(!row.hasAfter);}
        }
    }
    void diffUsesPathComponentsNotDottedNames() {
        const QJsonObject source{{"a.b",QJsonObject{{"c/d",1}}},{"a",QJsonObject{{"b.c/d",2}}}};
        const auto report=diff::build({},source,{},{});QCOMPARE(report.status.code,Code::Ok);QCOMPARE(report.rows.size(),qsizetype(2));
        QVERIFY(report.rows[0].path!=report.rows[1].path);
        QVERIFY(report.rows[0].path==QStringList({"a","b.c/d"}));QVERIFY(report.rows[1].path==QStringList({"a.b","c/d"}));
    }
    void diffRowBoundary_data() {
        QTest::addColumn<int>("count");QTest::newRow("limit")<<MaxDiffRows;QTest::newRow("over")<<(MaxDiffRows+1);
    }
    void diffRowBoundary() {
        QFETCH(int,count);QJsonObject source;for(int i=0;i<count;++i)source[QString::number(i)]=i;
        const auto report=diff::build({},source,{},{});
        if(count==MaxDiffRows){QCOMPARE(report.status.code,Code::Ok);QVERIFY(!report.truncated);QCOMPARE(report.rows.size(),qsizetype(count));}
        else {QCOMPARE(report.status.code,Code::LimitExceeded);QVERIFY(report.truncated);QCOMPARE(report.rows.size(),qsizetype(MaxDiffRows));}
        QVERIFY(report.accountedBytes<=MaxPreviewBytes);
    }
    void diffDepthBoundary_data() {
        QTest::addColumn<int>("depth");QTest::newRow("limit")<<MaxDiffDepth;QTest::newRow("over")<<(MaxDiffDepth+1);
    }
    void diffDepthBoundary() {
        QFETCH(int,depth);QJsonValue nested=1;for(int i=0;i<depth;++i)nested=QJsonObject{{"n",nested}};
        const auto report=diff::build({},nested.toObject(),{},{});
        if(depth==MaxDiffDepth){QCOMPARE(report.status.code,Code::Ok);QCOMPARE(report.rows.size(),qsizetype(1));QCOMPARE(report.rows.first().path.size(),qsizetype(depth));}
        else {QCOMPARE(report.status.code,Code::LimitExceeded);QVERIFY(report.truncated);QVERIFY(report.rows.isEmpty());}
        // Equality must not bypass the depth check.
        if(depth>MaxDiffDepth){const auto same=diff::build(nested.toObject(),nested.toObject(),nested.toObject(),{});QCOMPARE(same.status.code,Code::LimitExceeded);}
    }
    void repeatedLongAncestorPathsHitAggregateBound() {
        QJsonObject leaves;for(int i=0;i<300;++i)leaves[QString::number(i)]=i;
        const QJsonObject source{{QString(4000,'x'),leaves}};
        QVERIFY(QJsonDocument(source).toJson(QJsonDocument::Compact).size()<MaxMemberBytes);
        const auto report=diff::build({},source,{},{});QCOMPARE(report.status.code,Code::LimitExceeded);QVERIFY(report.truncated);
        QVERIFY(!report.rows.isEmpty());QVERIFY(report.rows.size()<MaxDiffRows);QVERIFY(report.accountedBytes<=MaxPreviewBytes);
    }
    void objectTypeReplacementDoesNotDuplicateSubtrees() {
        const QJsonObject current{{"unknown",QJsonObject{{"a",1},{"b",2}}}};
        const QJsonObject source{{"unknown","now a string"}};
        const auto report=diff::build(current,source,current,{});QCOMPARE(report.status.code,Code::Ok);QCOMPARE(report.rows.size(),qsizetype(1));
        QCOMPARE(report.rows.first().path,QStringList{"unknown"});QVERIFY(report.rows.first().before.isObject());
    }
    void diffCancellationReturnsNoUsableSuccess() {
        std::stop_source stop;stop.request_stop();Options options;options.cancellation=stop.get_token();options.generation=88;
        const auto report=diff::build({},{{"unknown",1}},{},{},options);QCOMPARE(report.status.code,Code::Cancelled);QCOMPARE(report.status.generation,quint64(88));QVERIFY(report.rows.isEmpty());
    }
    void realClaimOwnsOneSlotAndBlocksOfflineRestore() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);QCOMPARE(slot.number(),1);
        const auto identity=slot.claimIdentity();QVERIFY(identity.hasIdentity());Options immediate;immediate.lockTimeoutMs=0;
        {
            auto guard=ownership::acquire(identity,{dir.path()},immediate);QVERIFY(guard.held());QCOMPARE(guard.slot,1);
            FileLock competing(dir.filePath("sessions.json.lock"));QVERIFY(!competing.acquire(0));QVERIFY(competing.contended());
        }
        const auto offline=ownership::acquireOffline(1,{dir.path()},immediate);QVERIFY(!offline.held());QCOMPARE(offline.status.code,Code::Busy);
        QVERIFY(!ownership::acquire(glancelot::WindowClaim{},{dir.path()},immediate).held());
    }
    void sameProcessReleaseReclaimRevokesOldNonce() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);QCOMPARE(slot.number(),1);
        const auto old=slot.claimIdentity();QVERIFY(old.hasIdentity());
        slot.release();QVERIFY(slot.claim(0));QCOMPARE(slot.number(),1);const auto current=slot.claimIdentity();
        Options immediate;immediate.lockTimeoutMs=0;
        const auto stale=ownership::acquire(old,{dir.path()},immediate);QCOMPARE(stale.status.code,Code::Conflict);QVERIFY(!stale.held());
        const auto live=ownership::acquire(current,{dir.path()},immediate);QVERIFY(live.held());
    }
    void claimsInSameProcessAndOtherNamespacesStayDistinct() {
        QTemporaryDir dir,other;QVERIFY(dir.isValid()&&other.isValid());
        WindowSlot first(dir.path(),0),second(dir.path(),0);QCOMPARE(first.number(),1);QCOMPARE(second.number(),2);
        Options immediate;immediate.lockTimeoutMs=0;
        {const auto guard=ownership::acquire(first.claimIdentity(),{dir.path()},immediate);QVERIFY(guard.held());QCOMPARE(guard.slot,1);}
        {const auto guard=ownership::acquire(second.claimIdentity(),{dir.path()},immediate);QVERIFY(guard.held());QCOMPARE(guard.slot,2);}
        const auto substituted=ownership::acquire(first.claimIdentity(),{other.path()},immediate);QCOMPARE(substituted.status.code,Code::Conflict);QVERIFY(!substituted.held());
    }
    void changedLiveRecordCannotAuthorizeCapturedClaim_data() {
        QTest::addColumn<QString>("mutation");for(const auto* text:{"nonce","start","legacy","unknown-start"})QTest::newRow(text)<<QString::fromLatin1(text);
    }
    void changedLiveRecordCannotAuthorizeCapturedClaim() {
        QFETCH(QString,mutation);QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);
        const auto identity=slot.claimIdentity();const auto path=dir.filePath("sessions.json");auto session=QJsonDocument::fromJson(read(path)).object();
        auto live=session.value("live").toArray();auto row=live[0].toObject();
        if(mutation=="nonce")row["claimNonce"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
        else if(mutation=="legacy")row.remove("claimNonce");
        else row["stamp"]=mutation=="unknown-start"?1:row.value("stamp").toDouble()+1;
        live[0]=row;session["live"]=live;const auto changed=QJsonDocument(session).toJson(QJsonDocument::Compact);QVERIFY(write(path,changed));
        Options immediate;immediate.lockTimeoutMs=0;
        const auto refused=ownership::acquire(identity,{dir.path()},immediate);QCOMPARE(refused.status.code,Code::Conflict);QVERIFY(!refused.held());
        QCOMPARE(read(path),changed);
        if(mutation=="legacy"||mutation=="unknown-start") {
            const auto offline=ownership::acquireOffline(1,{dir.path()},immediate);QCOMPARE(offline.status.code,Code::Busy);QVERIFY(!offline.held());
        }
    }
    void offlineRefusesDamagedFutureAndAmbiguousBookkeeping_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("corrupt")<<QByteArray("broken");
        QTest::newRow("future")<<QByteArray("{\"schema\":2,\"counter\":0,\"live\":[]}");
        QTest::newRow("fractional")<<QByteArray("{\"schema\":1,\"counter\":1.5,\"live\":[]}");
        QTest::newRow("invalid-record")<<QByteArray("{\"schema\":1,\"counter\":1,\"live\":[{\"slot\":1}]}");
        QTest::newRow("oversized")<<QByteArray(64*1024+1,'x');
    }
    void offlineRefusesDamagedFutureAndAmbiguousBookkeeping() {
        QFETCH(QByteArray,bytes);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("sessions.json");QVERIFY(write(path,bytes));
        Options immediate;immediate.lockTimeoutMs=0;const auto guard=ownership::acquireOffline(1,{dir.path()},immediate);
        QVERIFY(!guard.held());QCOMPARE(guard.status.code,Code::Corrupt);QCOMPARE(read(path),bytes);
    }
    void offlineGuardDoesNotResetOrRewriteSessions() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const QByteArray bytes("{\"schema\":1,\"counter\":8,\"live\":[],\"unknown\":true}");
        const auto path=dir.filePath("sessions.json");QVERIFY(write(path,bytes));Options immediate;immediate.lockTimeoutMs=0;
        {const auto guard=ownership::acquireOffline(7,{dir.path()},immediate);QVERIFY(guard.held());QCOMPARE(guard.slot,7);QCOMPARE(read(path),bytes);}
        QCOMPARE(read(path),bytes);WindowSlot next(dir.path(),0);QCOMPARE(next.number(),1);
    }
    void ownershipGuardContentionAndCancellation() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);const auto identity=slot.claimIdentity();
        {
            FileLock held(dir.filePath("sessions.json.lock"));QVERIFY(held.acquire(0));Options immediate;immediate.lockTimeoutMs=0;
            const auto busy=ownership::acquire(identity,{dir.path()},immediate);QCOMPARE(busy.status.code,Code::Busy);QVERIFY(!busy.held());
            std::stop_source stop;stop.request_stop();Options cancelled;cancelled.cancellation=stop.get_token();cancelled.generation=71;
            const auto result=ownership::acquire(identity,{dir.path()},cancelled);QCOMPARE(result.status.code,Code::Cancelled);QCOMPARE(result.status.generation,quint64(71));
        }
    }
    void directoryBookkeepingCannotAuthorizeOfflineRestore() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QVERIFY(QDir(dir.path()).mkdir("sessions.json"));
        Options options;options.lockTimeoutMs=0;const auto guard=ownership::acquireOffline(1,{dir.path()},options);
        QCOMPARE(guard.status.code,Code::Corrupt);QVERIFY(!guard.held());QVERIFY(QFileInfo(dir.filePath("sessions.json")).isDir());
    }
    void capturedClaimAndOfflineGuardRetainPrivateRootOwner() {
        QTemporaryDir dir;QVERIFY(dir.isValid());auto owner=std::make_shared<int>(1);std::weak_ptr<int> weak=owner;
        glancelot::WindowClaim identity;
        {
            WindowSlot slot(dir.path(),0,owner);identity=slot.claimIdentity();QVERIFY(identity.hasIdentity());owner.reset();QVERIFY(!weak.expired());
        }
        QVERIFY(!weak.expired());Options options;options.lockTimeoutMs=0;
        const auto stale=ownership::acquire(identity,{dir.path()},options);QVERIFY(!stale.held());
        identity={};QVERIFY(weak.expired());
        owner=std::make_shared<int>(2);weak=owner;
        {
            auto guard=ownership::acquireOffline(1,{dir.path(),owner},options);QVERIFY(guard.held());owner.reset();QVERIFY(!weak.expired());
        }
        QVERIFY(weak.expired());
    }
    void publicPreviewApplyCreatesExactSealedPreimage() {
        QTemporaryDir dir,backupSource,packages;QVERIFY(dir.isValid()&&backupSource.isValid()&&packages.isValid());
        const QByteArray primary("{ \"schema\":1,\"wheel\":\"zoom\",\"unknown\":{\"future\":5} }"),lastGood("retained diagnostic backup"),damaged("old damaged bytes");
        QVERIFY(write(dir.filePath("settings.json"),primary));QVERIFY(write(dir.filePath("settings.json.bak"),lastGood));QVERIFY(write(dir.filePath("settings.json.damaged"),damaged));
        QVERIFY(write(backupSource.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\",\"unknown\":\"not applied\"}"));
        const auto captured=captureBackup({backupSource.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        const auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});
        QCOMPARE(proposed.status.code,Code::Ok);QVERIFY(proposed.plan.applicable());QVERIFY(!proposed.truncated);QVERIFY(proposed.fingerprint.startsWith("sf1:"));
        auto plan=proposed.plan;const auto applied=apply(plan);QCOMPARE(applied.status.code,Code::Committed);QVERIFY(!applied.preimage.isEmpty());
        const auto previous=package::read(applied.preimage);QVERIFY(previous.data);QCOMPARE(previous.data->primary.bytes,primary);QCOMPARE(previous.data->lastGood.bytes,lastGood);
        QCOMPARE(read(dir.filePath("settings.json.bak")),lastGood);QCOMPARE(read(dir.filePath("settings.json.damaged")),damaged);
        const auto current=readRaw({dir.path()});QVERIFY(current.snapshot);QCOMPARE(current.snapshot->revision,applied.resultingRevision);
        const auto object=QJsonDocument::fromJson(current.snapshot->primary.bytes).object();QCOMPARE(object.value("wheel"),QJsonValue("pan"));
        QCOMPARE(object.value("unknown").toObject().value("future"),QJsonValue(5));
        QCOMPARE(apply(plan).status.code,Code::Conflict); // cannot reuse a stale immutable plan
    }
    void publicNoChangeAndBackupOnlyConflictNeverCreatePreimages() {
        QTemporaryDir dir,packages;QVERIFY(dir.isValid()&&packages.isValid());const QByteArray primary("{ \"schema\":1, \"wheel\":\"pan\" }");
        QVERIFY(write(dir.filePath("settings.json"),primary));const auto captured=captureBackup({dir.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        const auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});QVERIFY(proposed.plan.applicable());
        const auto noop=apply(proposed.plan);QCOMPARE(noop.status.code,Code::NoChange);QVERIFY(noop.preimage.isEmpty());QCOMPARE(read(dir.filePath("settings.json")),primary);
        QVERIFY(!QFile::exists(dir.filePath("recovery-preimages")));
        QVERIFY(write(dir.filePath("settings.json.bak"),"{\"schema\":1}"));
        QCOMPARE(apply(proposed.plan).status.code,Code::Conflict);QVERIFY(!QFile::exists(dir.filePath("recovery-preimages")));
    }
    void changedPackageAfterPreviewAndAfterPreimageRefuses_data() {
        QTest::addColumn<bool>("afterPreimage");QTest::newRow("before-apply")<<false;QTest::newRow("after-preimage")<<true;
    }
    void changedPackageAfterPreviewAndAfterPreimageRefuses() {
        QFETCH(bool,afterPreimage);QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray primary("{\"schema\":1,\"wheel\":\"zoom\"}");QVERIFY(write(dir.filePath("settings.json"),primary));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\"}"));const auto captured=captureBackup({source.path()},packages.filePath("source"));
        const auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});QVERIFY(proposed.plan.applicable());
        const auto manifest=QDir(captured.destination).filePath("manifest.json");const auto original=read(manifest);bool changed=false;
        glancelot::settings_detail::TransactionHooks hooks;hooks.observe=[&](glancelot::settings_detail::Stage stage){
            if(afterPreimage&&stage==glancelot::settings_detail::Stage::AfterPreimage)changed=write(manifest,original+"\n");};Hooks installed(hooks);
        if(!afterPreimage)changed=write(manifest,original+"\n");
        const auto result=apply(proposed.plan);QVERIFY(changed);QCOMPARE(result.status.code,Code::Conflict);QCOMPARE(read(dir.filePath("settings.json")),primary);
        if(afterPreimage){QVERIFY(!result.preimage.isEmpty());QVERIFY(package::read(result.preimage).data);}
        else QVERIFY(result.preimage.isEmpty());
    }
    void invalidTruncatedAndFuturePreviewNeverYieldsPlan() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        QVERIFY(write(dir.filePath("settings.json"),"{\"schema\":1}"));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"surprise\"}"));
        const auto invalid=captureBackup({source.path()},packages.filePath("invalid"));const auto invalidPreview=preview({dir.path()},invalid.destination);
        QCOMPARE(invalidPreview.status.code,Code::Corrupt);QVERIFY(!invalidPreview.plan.applicable());QVERIFY(!invalidPreview.rows.isEmpty());QCOMPARE(invalidPreview.rows.first().treatment,Treatment::Invalid);
        QJsonObject large{{"schema",1}};for(int i=0;i<MaxDiffRows+1;++i)large[QString::number(i)]=i;
        QVERIFY(write(source.filePath("settings.json"),QJsonDocument(large).toJson(QJsonDocument::Compact)));
        const auto captured=captureBackup({source.path()},packages.filePath("large"));const auto limited=preview({dir.path()},captured.destination);
        QCOMPARE(limited.status.code,Code::LimitExceeded);QVERIFY(limited.truncated);QVERIFY(!limited.plan.applicable());
        QVERIFY(write(dir.filePath("settings.json"),"{\"schema\":3}"));
        const auto future=preview({dir.path()},captured.destination);QCOMPARE(future.status.code,Code::Unsupported);QVERIFY(!future.plan.applicable());
        QVERIFY(!QFile::exists(dir.filePath("recovery-preimages")));
    }
    void oversizedMergedReplacementNeverYieldsPlan() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const auto original=QJsonDocument(QJsonObject{{"schema",1},{"unknown",QString(int(MaxMemberBytes)-1024,'x')}}).toJson(QJsonDocument::Compact);
        QVERIFY(original.size()<MaxMemberBytes);QVERIFY(write(dir.filePath("settings.json"),original));
#ifdef Q_OS_WIN
        const QString folder="C:/"+QString(32760,'x');
#else
        const QString folder="/"+QString(32760,'x');
#endif
        const auto saved=QJsonDocument(QJsonObject{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",folder},{"saved",1}}}}}}).toJson(QJsonDocument::Compact);
        QVERIFY(saved.size()<MaxMemberBytes);QVERIFY(write(source.filePath("settings.json"),saved));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        const auto proposed=preview({dir.path()},captured.destination,OfflineSlotScope{SlotSection::Position,1});
        QCOMPARE(proposed.status.code,Code::LimitExceeded);QVERIFY(!proposed.plan.applicable());
        QCOMPARE(read(dir.filePath("settings.json")),original);QVERIFY(!QFile::exists(dir.filePath("recovery-preimages")));
    }
    void publicOwnedAndOfflinePositionRestoreKeepOtherSlots() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
#ifdef Q_OS_WIN
        const QString folder="C:/generated",image="C:/generated/a.png";
#else
        const QString folder="/generated",image="/generated/a.png";
#endif
        QVERIFY(write(dir.filePath("settings.json"),"{\"schema\":1,\"windows\":{\"2\":{\"image\":\"untouched\",\"future\":true}}}"));
        const QJsonObject saved{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",image},{"saved",1}}}}}};
        QVERIFY(write(source.filePath("settings.json"),QJsonDocument(saved).toJson(QJsonDocument::Compact)));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));WindowSlot slot(dir.path(),0);QCOMPARE(slot.number(),1);
        const auto proposed=preview({dir.path()},captured.destination,OwnedSlotScope{SlotSection::Position,slot.claimIdentity()});QCOMPARE(proposed.status.code,Code::Ok);QVERIFY(proposed.plan.applicable());
        QCOMPARE(apply(proposed.plan).status.code,Code::Committed);
        auto raw=readRaw({dir.path()});QVERIFY(raw.snapshot);auto current=QJsonDocument::fromJson(raw.snapshot->primary.bytes).object();
        QCOMPARE(current.value("windows").toObject().value("2").toObject().value("image"),QJsonValue("untouched"));
        QCOMPARE(current.value("recovery").toObject().value("windows").toObject().value("1").toObject().value("origin"),QJsonValue("restore"));
        slot.release();const auto offline=preview({dir.path()},captured.destination,OfflineSlotScope{SlotSection::Position,1});QVERIFY(offline.plan.applicable());
        QCOMPARE(apply(offline.plan).status.code,Code::Committed);raw=readRaw({dir.path()});QVERIFY(raw.snapshot);current=QJsonDocument::fromJson(raw.snapshot->primary.bytes).object();
        const auto record=current.value("recovery").toObject().value("windows").toObject().value("1").toObject();
        QCOMPARE(record.value("state"),QJsonValue("clean"));QCOMPARE(record.value("pid").toInteger(),qint64(0));QVERIFY(record.value("claimNonce").toString().isEmpty());
    }
    void immutablePlanIgnoresEditedPreviewFieldsAndBindsScope() {
        QTemporaryDir dir,other,source,packages;QVERIFY(dir.isValid()&&other.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray primary("{\"schema\":1,\"wheel\":\"zoom\"}");QVERIFY(write(dir.filePath("settings.json"),primary));QVERIFY(write(other.filePath("settings.json"),primary));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\"}"));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});
        QVERIFY(proposed.plan.applicable());const auto differentNamespace=preview({other.path()},captured.destination,GlobalScope{GlobalSection::Viewing});
        QVERIFY(differentNamespace.plan.applicable());QVERIFY(proposed.fingerprint!=differentNamespace.fingerprint);
        const auto differentScope=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Browsing});QVERIFY(differentScope.plan.applicable());QVERIFY(proposed.fingerprint!=differentScope.fingerprint);
        proposed.rows.clear();proposed.expectedCurrentRevision="forged";proposed.namespaceId="forged";proposed.fingerprint="forged";proposed.truncated=true;
        QCOMPARE(apply(proposed.plan).status.code,Code::Committed);QCOMPARE(read(other.filePath("settings.json")),primary);
        QVERIFY(read(dir.filePath("settings.json")).contains("pan"));QCOMPARE(apply(RestorePlan{}).status.code,Code::Corrupt);
    }
    void lastGoodSourceIsExplicitAndCorruptCurrentIsPreserved() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray damaged("corrupt original bytes"),backup("{\"schema\":1,\"unknown\":17,\"wheel\":\"zoom\"}");
        QVERIFY(write(dir.filePath("settings.json"),damaged));QVERIFY(write(dir.filePath("settings.json.bak"),backup));
        QVERIFY(write(source.filePath("settings.json"),"diagnostic corrupt package primary"));QVERIFY(write(source.filePath("settings.json.bak"),"{\"schema\":1,\"wheel\":\"pan\"}"));
        const auto captured=captureBackup({source.path()},packages.filePath("source"));
        const auto noSubstitution=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});QCOMPARE(noSubstitution.status.code,Code::Corrupt);QVERIFY(!noSubstitution.plan.applicable());
        const auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing},SourceMember::LastGood);
        QCOMPARE(proposed.status.code,Code::Ok);QVERIFY(proposed.plan.applicable());QVERIFY(!proposed.status.detail.isEmpty());
        const auto restored=apply(proposed.plan);QCOMPARE(restored.status.code,Code::Committed);
        const auto preimage=package::read(restored.preimage);QVERIFY(preimage.data);QCOMPARE(preimage.data->primary.bytes,damaged);QCOMPARE(preimage.data->lastGood.bytes,backup);
        QCOMPARE(read(dir.filePath("settings.json.bak")),backup);const auto object=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object();
        QCOMPARE(object.value("unknown").toInt(),17);QCOMPARE(object.value("wheel"),QJsonValue("pan"));
    }
    void publicPreimageFailureAndCancellationKeepOriginal() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray primary("{\"schema\":1,\"wheel\":\"zoom\"}");QVERIFY(write(dir.filePath("settings.json"),primary));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\"}"));const auto captured=captureBackup({source.path()},packages.filePath("source"));
        const auto proposed=preview({dir.path()},captured.destination,GlobalScope{GlobalSection::Viewing});QVERIFY(proposed.plan.applicable());
        QVERIFY(write(dir.filePath("recovery-preimages"),"block root"));const auto blocked=apply(proposed.plan);QCOMPARE(blocked.status.code,Code::WriteFailed);
        QCOMPARE(read(dir.filePath("settings.json")),primary);QVERIFY(QFile::remove(dir.filePath("recovery-preimages")));
        std::stop_source stop;Options options;options.cancellation=stop.get_token();package::Hooks hooks;
        hooks.observe=[&](package::Stage stage){if(stage==package::Stage::BeforeSeal)stop.request_stop();};
        const auto old=package::hooks;package::hooks=&hooks;const auto restore=qScopeGuard([&]{package::hooks=old;});
        const auto cancelled=apply(proposed.plan,options);QCOMPARE(cancelled.status.code,Code::Cancelled);QCOMPARE(read(dir.filePath("settings.json")),primary);
        QVERIFY(!cancelled.preimage.isEmpty());QVERIFY(!package::read(cancelled.preimage).data);
    }
    void checkpointLifecyclePreservesUnknownsAndEmptyFolder() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
#ifdef Q_OS_WIN
        const QString folder="C:/generated-empty";
#else
        const QString folder="/generated-empty";
#endif
        const QJsonObject initial{{"schema",1},{"future",9},{"windows",QJsonObject{{"1",QJsonObject{{"future",true}}},{"2",QJsonObject{{"image","untouched"}}}}}};
        QVERIFY(write(dir.filePath("settings.json"),QJsonDocument(initial).toJson()));WindowSlot slot(dir.path(),0);QVERIFY(slot.claimIdentity().hasIdentity());
        const auto begun=beginWindow(slot.claimIdentity());QCOMPARE(begun.status.code,Code::Committed);QVERIFY(!begun.previous.hasPosition);QVERIFY(!begun.previous.priorDirty);
        const auto saved=checkpoint(slot.claimIdentity(),{folder,{},1});QCOMPARE(saved.status.code,Code::Committed);QCOMPARE(saved.sequence,quint64(1));QVERIFY(!saved.checkpointRevision.isEmpty());
        const auto root=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object();
        const auto windows=root.value("windows").toObject();const auto position=windows.value("1").toObject();
        QCOMPARE(position.value("folder"),QJsonValue(folder));QCOMPARE(position.value("image"),QJsonValue(""));QCOMPARE(position.value("future"),QJsonValue(true));
        QCOMPARE(root.value("future"),QJsonValue(9));QCOMPARE(windows.value("2"),initial.value("windows").toObject().value("2"));
        auto record=root.value("recovery").toObject().value("windows").toObject().value("1").toObject();
        QCOMPARE(record.value("state"),QJsonValue("dirty"));QCOMPARE(record.value("origin"),QJsonValue("navigation"));QCOMPARE(record.value("sequence"),QJsonValue("1"));
        QCOMPARE(record.value("checkpointRevision"),QJsonValue(saved.checkpointRevision));
        QCOMPARE(finishWindow(slot.claimIdentity(),saved.checkpointRevision,1).code,Code::Committed);
        QCOMPARE(finishWindow(slot.claimIdentity(),saved.checkpointRevision,1).code,Code::NoChange);
        QVERIFY(!QFile::exists(dir.filePath("recovery-preimages")));QVERIFY(QFile::exists(dir.filePath("settings.json.bak")));
    }
    void checkpointStaleCompletionCannotCleanNewerPosition() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);
        const auto first=checkpoint(slot.claimIdentity(),{{},{},1});QCOMPARE(first.status.code,Code::Committed);
        const auto second=checkpoint(slot.claimIdentity(),{{},{},3});QCOMPARE(second.status.code,Code::Committed);
        const auto bytes=read(dir.filePath("settings.json"));
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},2}).status.code,Code::Conflict);
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},3}).status.code,Code::Conflict);
        QCOMPARE(finishWindow(slot.claimIdentity(),first.checkpointRevision,1).code,Code::Conflict);
        QCOMPARE(finishWindow(slot.claimIdentity(),second.checkpointRevision,2).code,Code::Conflict);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::NoChange); // cannot reset sequence3
        QCOMPARE(read(dir.filePath("settings.json")),bytes);
        QCOMPARE(finishWindow(slot.claimIdentity(),second.checkpointRevision,3).code,Code::Committed);
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},4}).status.code,Code::Conflict); // finalized claim
    }
    void beginCapturesDirtyPayloadBeforeNewClaimAndNoNavigationExit() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QString checkpointId;
        {
            WindowSlot old(dir.path(),0);QCOMPARE(beginWindow(old.claimIdentity()).status.code,Code::Committed);
#ifdef Q_OS_WIN
            const QString folder="C:/generated";
#else
            const QString folder="/generated";
#endif
            const auto saved=checkpoint(old.claimIdentity(),{folder,{},7});QCOMPARE(saved.status.code,Code::Committed);checkpointId=saved.checkpointRevision;
        } // simulate dirty exit: release the session claim without finishWindow
        WindowSlot next(dir.path(),0);QCOMPARE(next.number(),1);
        const auto begun=beginWindow(next.claimIdentity());QCOMPARE(begun.status.code,Code::Committed);
        QVERIFY(begun.previous.hasPosition);QVERIFY(begun.previous.priorDirty);QCOMPARE(begun.previous.checkpointRevision,checkpointId);
        QCOMPARE(finishWindow(next.claimIdentity(),checkpointId,0).code,Code::Committed);
        const auto root=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object();
        const auto row=root.value("recovery").toObject().value("windows").toObject().value("1").toObject();
        QCOMPARE(row.value("sequence"),QJsonValue("0"));QCOMPARE(row.value("state"),QJsonValue("clean"));QCOMPARE(row.value("checkpointRevision"),QJsonValue(checkpointId));
    }
    void checkpointFailureLeavesPrimaryDirtyAndCannotAcknowledge() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);
        const auto before=read(dir.filePath("settings.json"));
        glancelot::settings_detail::TransactionHooks hooks;hooks.fail=[](glancelot::settings_detail::Stage stage){return stage==glancelot::settings_detail::Stage::BeforeReplace;};
        {
            Hooks installed(hooks);const auto failed=checkpoint(slot.claimIdentity(),{{},{},1});QCOMPARE(failed.status.code,Code::WriteFailed);
            QVERIFY(failed.checkpointRevision.isEmpty());QCOMPARE(failed.sequence,quint64(0));QCOMPARE(read(dir.filePath("settings.json")),before);
            QCOMPARE(finishWindow(slot.claimIdentity(),{},0).code,Code::WriteFailed);QCOMPARE(read(dir.filePath("settings.json")),before);
        }
        const auto succeeded=checkpoint(slot.claimIdentity(),{{},{},1});QCOMPARE(succeeded.status.code,Code::Committed);
        QCOMPARE(finishWindow(slot.claimIdentity(),succeeded.checkpointRevision,1).code,Code::Committed);
    }
    void checkpointInvalidInputContentionAndCancellationDoNotWrite() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},1}).status.code,Code::Conflict);QVERIFY(!QFile::exists(dir.filePath("settings.json")));
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);const auto before=read(dir.filePath("settings.json"));
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},0}).status.code,Code::Corrupt);
        QCOMPARE(checkpoint(slot.claimIdentity(),{"relative",{},1}).status.code,Code::Corrupt);
        QCOMPARE(checkpoint(slot.claimIdentity(),{QString(32769,'x'),{},1}).status.code,Code::Corrupt);
        Options options;options.lockTimeoutMs=0;options.generation=91;
        {
            FileLock held(dir.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
            QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},1},options).status.code,Code::Busy);
            QCOMPARE(finishWindow(slot.claimIdentity(),{},0,options).code,Code::Busy);
        }
        std::stop_source stop;stop.request_stop();options.cancellation=stop.get_token();
        const auto cancelled=checkpoint(slot.claimIdentity(),{{},{},1},options);QCOMPARE(cancelled.status.code,Code::Cancelled);QCOMPARE(cancelled.status.generation,quint64(91));
        QCOMPARE(read(dir.filePath("settings.json")),before);
        const auto stale=slot.claimIdentity();slot.release();QCOMPARE(beginWindow(stale).status.code,Code::Conflict);
        QCOMPARE(checkpoint(stale,{{},{},1}).status.code,Code::Conflict);QCOMPARE(finishWindow(stale,{},0).code,Code::Conflict);
    }
    void checkpointKeepsUnknownRecoveryAndOtherSlots() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);
        auto root=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object();
        auto recovery=root.value("recovery").toObject();recovery["future"]=QJsonObject{{"flag",true}};
        auto windows=recovery.value("windows").toObject();auto own=windows.value("1").toObject();own["future"]=QJsonObject{{"value",5}};
        const QJsonObject other{{"version",99},{"unknown","preserve"}};windows["1"]=own;windows["2"]=other;recovery["windows"]=windows;root["recovery"]=recovery;
        QVERIFY(write(dir.filePath("settings.json"),QJsonDocument(root).toJson()));
        const auto saved=checkpoint(slot.claimIdentity(),{{},{},1});QCOMPARE(saved.status.code,Code::Committed);
        QCOMPARE(finishWindow(slot.claimIdentity(),saved.checkpointRevision,1).code,Code::Committed);
        const auto current=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object().value("recovery").toObject();
        QCOMPARE(current.value("future"),recovery.value("future"));QCOMPARE(current.value("windows").toObject().value("2"),QJsonValue(other));
        QCOMPARE(current.value("windows").toObject().value("1").toObject().value("future"),own.value("future"));
    }
    void checkpointCancellationAfterCommitStillAcknowledgesRevision() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);
        std::stop_source stop;Options options;options.cancellation=stop.get_token();
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[&](glancelot::settings_detail::Stage stage){if(stage==glancelot::settings_detail::Stage::AfterReplace)stop.request_stop();};
        CheckpointResult saved;
        {Hooks installed(hooks);saved=checkpoint(slot.claimIdentity(),{{},{},1},options);}
        QVERIFY(stop.stop_requested());QCOMPARE(saved.status.code,Code::Committed);QCOMPARE(saved.sequence,quint64(1));QVERIFY(!saved.checkpointRevision.isEmpty());
        QCOMPARE(finishWindow(slot.claimIdentity(),saved.checkpointRevision,1).code,Code::Committed);
    }
    void checkpointRevokedBeforeReplaceLeavesDirtyOriginal() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);const auto claim=slot.claimIdentity();
        QCOMPARE(beginWindow(claim).status.code,Code::Committed);const auto original=read(dir.filePath("settings.json"));
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[&](glancelot::settings_detail::Stage stage){if(stage==glancelot::settings_detail::Stage::BeforeReplace)slot.release();};
        {Hooks installed(hooks);const auto refused=checkpoint(claim,{{},{},1});QCOMPARE(refused.status.code,Code::Conflict);QVERIFY(refused.checkpointRevision.isEmpty());}
        QCOMPARE(read(dir.filePath("settings.json")),original);QCOMPARE(finishWindow(claim,{},0).code,Code::Conflict);
        QVERIFY(slot.claim(0));QCOMPARE(beginWindow(slot.claimIdentity()).status.code,Code::Committed);
        QCOMPARE(checkpoint(claim,{{},{},1}).status.code,Code::Conflict); // same process, new nonce
        QCOMPARE(finishWindow(slot.claimIdentity(),{},0).code,Code::Committed); // no-navigation exit
    }
    void checkpointFutureAndMalformedRecoveryPreserved_data() {
        QTest::addColumn<QJsonValue>("recovery");QTest::addColumn<Code>("expected");
        QTest::newRow("future-root")<<QJsonValue(QJsonObject{{"version",2}})<<Code::Unsupported;
        QTest::newRow("malformed-root")<<QJsonValue("broken")<<Code::Corrupt;
        QTest::newRow("missing-version")<<QJsonValue(QJsonObject{{"future",true}})<<Code::Corrupt;
        QTest::newRow("future-slot")<<QJsonValue(QJsonObject{{"version",1},{"windows",QJsonObject{{"1",QJsonObject{{"version",2}}}}}})<<Code::Unsupported;
        QTest::newRow("malformed-sequence")<<QJsonValue(QJsonObject{{"version",1},{"windows",QJsonObject{{"1",QJsonObject{{"version",1},{"sequence","01"}}}}}})<<Code::Corrupt;
    }
    void checkpointFutureAndMalformedRecoveryPreserved() {
        QFETCH(QJsonValue,recovery);QFETCH(Code,expected);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"recovery",recovery}}).toJson();QVERIFY(write(dir.filePath("settings.json"),bytes));
        WindowSlot slot(dir.path(),0);const auto begun=beginWindow(slot.claimIdentity());QCOMPARE(begun.status.code,expected);QVERIFY(!begun.previous.hasPosition);
        QCOMPARE(checkpoint(slot.claimIdentity(),{{},{},1}).status.code,expected);QCOMPARE(finishWindow(slot.claimIdentity(),{},0).code,expected);
        QCOMPARE(read(dir.filePath("settings.json")),bytes);QVERIFY(!QFile::exists(dir.filePath("settings.json.bak")));
    }
    void headlessMaintenanceBackupDiffAndCheckedRestore() {
        using namespace glancelot::startup;QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray original("{\"schema\":1,\"wheel\":\"zoom\",\"unknown\":null}");QVERIFY(write(dir.filePath("settings.json"),original));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1,\"wheel\":\"pan\",\"unknown\":{\"slash/key.dot\":true}}"));
        Intent command;command.mode=Mode::Backup;command.packageDirectory=packages.filePath("source");
        auto result=maintenance(command,{source.path()});QCOMPARE(result.exitCode,0);QVERIFY(result.json.size()<=MaxPreviewBytes);
        auto json=QJsonDocument::fromJson(result.json).object();QCOMPARE(json.value("schema"),QJsonValue(1));QCOMPARE(json.value("code"),QJsonValue("Ok"));
        QVERIFY(!json.value("package_revision").toString().isEmpty());
        command.mode=Mode::Diff;command.global=GlobalSection::Viewing;Options options;options.generation=18446744073709551615ULL;
        result=maintenance(command,{dir.path()},options);QCOMPARE(result.exitCode,0);json=QJsonDocument::fromJson(result.json).object();
        QCOMPARE(json.value("generation").toString(),QStringLiteral("18446744073709551615"));QVERIFY(!json.value("truncated").toBool());
        bool applied=false,preservedNull=false;
        for(const auto& value:json.value("rows").toArray()) {
            const auto row=value.toObject();QVERIFY(row.value("path").isArray());
            if(row.value("path").toArray()==QJsonArray{"wheel"}) {QCOMPARE(row.value("treatment"),QJsonValue("apply"));QCOMPARE(row.value("change"),QJsonValue("changed"));applied=true;}
            if(row.value("path").toArray()==QJsonArray{"unknown"}) {QVERIFY(row.value("had_before").toBool());QVERIFY(row.value("before").isNull());QCOMPARE(row.value("treatment"),QJsonValue("preserve"));preservedNull=true;}
        }
        QVERIFY(applied&&preservedNull);command.mode=Mode::Restore;command.expectedFingerprint=json.value("fingerprint").toString();
        const auto accepted=maintenance(command,{dir.path()});QCOMPARE(accepted.exitCode,0);json=QJsonDocument::fromJson(accepted.json).object();
        QCOMPARE(json.value("code"),QJsonValue("Committed"));const auto preimage=package::read(json.value("preimage").toString());QVERIFY(preimage.data);QCOMPARE(preimage.data->primary.bytes,original);
        const auto restored=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object();QCOMPARE(restored.value("wheel"),QJsonValue("pan"));QVERIFY(restored.value("unknown").isNull());
        QCOMPARE(maintenance(command,{dir.path()}).exitCode,3); // old CLI fingerprint is no longer current
        command.mode=Mode::Diff;result=maintenance(command,{dir.path()});json=QJsonDocument::fromJson(result.json).object();command.expectedFingerprint=json.value("fingerprint").toString();
        command.mode=Mode::Restore;result=maintenance(command,{dir.path()});QCOMPARE(result.exitCode,0);json=QJsonDocument::fromJson(result.json).object();
        QCOMPARE(json.value("code"),QJsonValue("NoChange"));QVERIFY(json.value("preimage").toString().isEmpty());
        QVERIFY(!QCoreApplication::instance()->inherits("QGuiApplication"));
    }
    void headlessMaintenanceOfflineRestoreUsesHistoricalCleanRecord() {
        using namespace glancelot::startup;QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
#ifdef Q_OS_WIN
        const QString folder="C:/generated";
#else
        const QString folder="/generated";
#endif
        const auto saved=QJsonDocument(QJsonObject{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",""},{"saved",1}}}}}}).toJson();
        QVERIFY(write(source.filePath("settings.json"),saved));const auto captured=captureBackup({source.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        Intent command;command.mode=Mode::Diff;command.packageDirectory=captured.destination;command.slotSection=SlotSection::Position;command.slot=1;
        {WindowSlot live(dir.path(),0);QCOMPARE(maintenance(command,{dir.path()}).exitCode,7);QVERIFY(!QFile::exists(dir.filePath("settings.json")));}
        auto result=maintenance(command,{dir.path()});QCOMPARE(result.exitCode,0);command.expectedFingerprint=QJsonDocument::fromJson(result.json).object().value("fingerprint").toString();
        command.mode=Mode::Restore;result=maintenance(command,{dir.path()});QCOMPARE(result.exitCode,0);
        WindowSlot next(dir.path(),0);const auto resumed=beginWindow(next.claimIdentity());QCOMPARE(resumed.status.code,Code::Committed);
        QVERIFY(resumed.previous.hasPosition);QVERIFY(!resumed.previous.priorDirty);QCOMPARE(resumed.previous.folder,folder);
    }
    void headlessMaintenanceTruncationAndCancellationHaveBoundedErrors() {
        using namespace glancelot::startup;QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        Intent command;QCOMPARE(maintenance(command,{dir.path()}).exitCode,2);QVERIFY(!QFile::exists(dir.filePath("settings.json")));
        command.mode=Mode::Backup;command.packageDirectory=packages.filePath("cancelled");std::stop_source stop;stop.request_stop();Options options;options.cancellation=stop.get_token();
        const auto cancelled=maintenance(command,{dir.path()},options);QCOMPARE(cancelled.exitCode,4);QCOMPARE(QJsonDocument::fromJson(cancelled.json).object().value("code"),QJsonValue("Cancelled"));
        QVERIFY(!QFile::exists(command.packageDirectory));
        QJsonObject large{{"schema",1}};for(int i=0;i<MaxDiffRows+1;++i)large[QString::number(i)]=i;
        QVERIFY(write(source.filePath("settings.json"),QJsonDocument(large).toJson()));const auto captured=captureBackup({source.path()},packages.filePath("source"));QCOMPARE(captured.status.code,Code::Ok);
        command.mode=Mode::Diff;command.packageDirectory=captured.destination;const auto limited=maintenance(command,{dir.path()});QCOMPARE(limited.exitCode,6);QVERIFY(limited.json.size()<=MaxPreviewBytes);
        const auto json=QJsonDocument::fromJson(limited.json).object();QCOMPARE(json.value("code"),QJsonValue("LimitExceeded"));QVERIFY(json.value("truncated").toBool());
        QVERIFY(json.value("fingerprint").toString().isEmpty());QVERIFY(json.value("rows").toArray().size()<=MaxDiffRows);QVERIFY(!QFile::exists(dir.filePath("settings.json")));
    }
    void maintenanceExitCodesRemainStable() {
        using namespace glancelot::startup;
        const std::pair<Code,int> expected[]={{Code::Ok,0},{Code::NoChange,0},{Code::Committed,0},{Code::Conflict,3},{Code::Cancelled,4},{Code::Unsupported,5},{Code::Corrupt,6},{Code::LimitExceeded,6},{Code::Busy,7},{Code::Unreadable,7},{Code::WriteFailed,7}};
        QSet<QString> names;for(const auto& [code,value]:expected){QCOMPARE(exitCode(code),value);QVERIFY(!names.contains(codeName(code)));names.insert(codeName(code));}
    }
    void recoverySessionBoundsWorkToActiveAndLatestPending() {
        struct Gate {std::mutex mutex;std::condition_variable changed;bool entered{},released{};std::atomic_bool enabled{};std::atomic_int replacements{};};
        const auto gate=std::make_shared<Gate>();
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[gate](glancelot::settings_detail::Stage stage){
            if(stage!=glancelot::settings_detail::Stage::BeforeReplace||!gate->enabled)return;
            if(++gate->replacements!=1)return;
            std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.notify_all();gate->changed.wait(lock,[&]{return gate->released;});
        };
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);Hooks installed(hooks);Session session(slot.claimIdentity());
        QVERIFY(session.waitForIdle(3000));QVERIFY(session.snapshot().begun);gate->enabled=true;
        // Ensure assertion failures cannot leave a blocked background writer.
        const auto release=qScopeGuard([gate]{std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();});
        QVERIFY(session.accept({{},{},1}));
        {std::unique_lock lock(gate->mutex);QVERIFY(gate->changed.wait_for(lock,std::chrono::seconds(3),[&]{return gate->entered;}));}
        for(quint64 sequence=2;sequence<=100;++sequence)QVERIFY(session.accept({{},{},sequence}));
        QCOMPARE(session.snapshot().acceptedSequence,quint64(100));QVERIFY(!session.accept({{},{},99}));
        QVERIFY(!session.close(0));QVERIFY(!session.accept({{},{},101}));
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
        QVERIFY(session.waitForIdle(3000));const auto state=session.snapshot();QVERIFY(state.closed);QCOMPARE(state.finalStatus.code,Code::Committed);
        QCOMPARE(state.persistedSequence,quint64(100));QCOMPARE(gate->replacements.load(),3); // checkpoint1, checkpoint100, clean
        const auto record=QJsonDocument::fromJson(read(dir.filePath("settings.json"))).object().value("recovery").toObject().value("windows").toObject().value("1").toObject();
        QCOMPARE(record.value("sequence"),QJsonValue("100"));QCOMPARE(record.value("state"),QJsonValue("clean"));
    }
    void recoverySessionFailedLatestCheckpointNeverCleans() {
        const auto failing=std::make_shared<std::atomic_bool>(false);glancelot::settings_detail::TransactionHooks hooks;
        hooks.fail=[failing](glancelot::settings_detail::Stage stage){return failing->load()&&stage==glancelot::settings_detail::Stage::BeforeReplace;};
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);Hooks installed(hooks);Session session(slot.claimIdentity());
        QVERIFY(session.waitForIdle(3000));QVERIFY(session.snapshot().begun);const auto original=read(dir.filePath("settings.json"));
        *failing=true;QVERIFY(session.accept({{},{},1}));QVERIFY(session.waitForIdle(3000));
        QCOMPARE(session.snapshot().lastAttempt.status.code,Code::WriteFailed);QVERIFY(!session.close(3000));
        const auto state=session.snapshot();QVERIFY(state.closed);QCOMPARE(state.persistedSequence,quint64(0));QCOMPARE(state.finalStatus.code,Code::WriteFailed);
        QCOMPARE(read(dir.filePath("settings.json")),original);
    }
    void recoverySessionRetainsPrivateLifetimeAfterOwnerTeardown() {
        struct Gate {std::mutex mutex;std::condition_variable changed;bool entered{},released{};std::atomic_bool enabled{};};
        const auto gate=std::make_shared<Gate>();glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[gate](glancelot::settings_detail::Stage stage){
            if(stage!=glancelot::settings_detail::Stage::BeforeReplace||!gate->enabled)return;
            std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.notify_all();gate->changed.wait(lock,[&]{return gate->released;});
        };
        QTemporaryDir dir;QVERIFY(dir.isValid());auto lifetime=std::make_shared<int>(1);std::weak_ptr<const void> weak=lifetime;
        auto slot=std::make_unique<WindowSlot>(dir.path(),0,lifetime);lifetime.reset();Hooks installed(hooks);auto session=std::make_unique<Session>(slot->claimIdentity());
        QVERIFY(session->waitForIdle(3000));QVERIFY(session->snapshot().begun);const auto original=read(dir.filePath("settings.json"));gate->enabled=true;
        const auto release=qScopeGuard([gate]{std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();});
        QVERIFY(session->accept({{},{},1}));
        {std::unique_lock lock(gate->mutex);QVERIFY(gate->changed.wait_for(lock,std::chrono::seconds(3),[&]{return gate->entered;}));}
        session.reset();slot.reset();QVERIFY(!weak.expired());
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
        // Both controller session and slot are gone; only the running job owns
        // the private-root lifetime until its failed transaction has returned.
        QTRY_VERIFY_WITH_TIMEOUT(([&]{FileLock lock(dir.filePath("settings.json.lock"));return lock.acquire(0);})(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(weak.expired(),3000);
        QCOMPARE(read(dir.filePath("settings.json")),original);
    }
    void recoverySessionNoNavigationCanCloseAndInvalidClaimCannotBegin() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);Session session(slot.claimIdentity());
        QVERIFY(session.close(3000));const auto state=session.snapshot();QVERIFY(state.closed&&state.begun);QCOMPARE(state.acceptedSequence,quint64(0));
        QCOMPARE(state.finalStatus.code,Code::Committed);
        Session invalid(glancelot::WindowClaim{});QVERIFY(invalid.waitForIdle(3000));QVERIFY(!invalid.snapshot().begun);
        QCOMPARE(invalid.snapshot().beginning.status.code,Code::Conflict);QVERIFY(!invalid.close(3000));
    }
    void recoverySessionCannotCleanWhenCallerHasUnqueuedSuccess() {
        QTemporaryDir dir;QVERIFY(dir.isValid());WindowSlot slot(dir.path(),0);Session session(slot.claimIdentity());
        QVERIFY(session.waitForIdle(3000));QVERIFY(session.snapshot().begun);const auto original=read(dir.filePath("settings.json"));
        QVERIFY(!session.close(3000,false));QVERIFY(session.snapshot().closed);QCOMPARE(session.snapshot().finalStatus.code,Code::WriteFailed);
        QCOMPARE(read(dir.filePath("settings.json")),original);
    }
    void revokedOwnerImmediatelyBeforeReplaceCannotApply() {
        QTemporaryDir dir,source,packages;QVERIFY(dir.isValid()&&source.isValid()&&packages.isValid());
        const QByteArray primary("{\"schema\":1}");QVERIFY(write(dir.filePath("settings.json"),primary));
        QVERIFY(write(source.filePath("settings.json"),"{\"schema\":1}"));const auto captured=captureBackup({source.path()},packages.filePath("source"));
        WindowSlot slot(dir.path(),0);const auto proposed=preview({dir.path()},captured.destination,OwnedSlotScope{SlotSection::Position,slot.claimIdentity()});QVERIFY(proposed.plan.applicable());
        bool revoked=false;glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[&](glancelot::settings_detail::Stage stage){if(stage==glancelot::settings_detail::Stage::BeforeReplace){slot.release();revoked=true;}};
        Hooks installed(hooks);const auto refused=apply(proposed.plan);QVERIFY(revoked);QCOMPARE(refused.status.code,Code::Conflict);
        QCOMPARE(read(dir.filePath("settings.json")),primary);QVERIFY(!refused.preimage.isEmpty());QVERIFY(package::read(refused.preimage).data);
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto arguments=app.arguments();
    if(arguments.size()==5&&arguments.at(1)=="--restore-crash-helper") {
        bool valid=false;const int point=arguments.at(4).toInt(&valid);
        if(!valid||point<int(glancelot::settings_detail::Stage::BeforeEvidence)||point>int(glancelot::settings_detail::Stage::AfterReplace))return 80;
        const auto proposed=preview({arguments.at(2)},arguments.at(3),GlobalScope{GlobalSection::Viewing});
        if(proposed.status.code!=Code::Ok||!proposed.plan.applicable())return 81;
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[&](glancelot::settings_detail::Stage stage) {
            if(int(stage)!=point)return;
            std::fputs("restore-stage-ready\n",stdout);std::fflush(stdout);
            // Only the parent test terminates this owned child after the real
            // transaction reaches the selected boundary with its lock held.
            for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));
        };
        glancelot::settings_detail::transactionHooks=&hooks;
        apply(proposed.plan);
        return 82;
    }
    RecoveryTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "recovery_tests.moc"
