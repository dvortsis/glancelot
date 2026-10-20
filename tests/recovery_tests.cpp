#include "glancelot/settings_recovery.hpp"
#include "file_lock.hpp"
#include "settings_transaction.hpp"
#include "settings_package.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include "glancelot/settings_file.hpp"
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
};
QTEST_GUILESS_MAIN(RecoveryTests)
#include "recovery_tests.moc"
