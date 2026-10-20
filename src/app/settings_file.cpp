#include "glancelot/settings_file.hpp"
#include "file_lock.hpp"
#include "settings_transaction.hpp"
#include "settings_raw.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QThread>

namespace glancelot {
namespace {
constexpr qint64 kMaxSettingsBytes = 1024 * 1024;
// Interactive writes never sleep behind another process or retry a replacement.
bool interactiveThread() {
    const auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

SettingsStatus parseExisting(const QString& path, QJsonObject& out, QByteArray* original = nullptr) {
    out = {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxSettingsBytes) return {SettingsStatus::Unreadable};
    QJsonParseError error;
    const auto bytes=file.read(kMaxSettingsBytes+1);
    if(file.error()!=QFileDevice::NoError || bytes.size()>kMaxSettingsBytes) return {SettingsStatus::Unreadable};
    if(original)*original=bytes;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {SettingsStatus::Corrupt};
    const auto object = document.object();
    if (object.value("schema").toInt() != 1) return {SettingsStatus::Unsupported};
    out = object;
    return {};
}

// The last-known-good copy, only when it is itself a real schema-1 file.
bool validBackup(const QString& path, QJsonObject& out) {
    QFile file(path + ".bak");
    if (!file.exists()) return false;
    return parseExisting(path + ".bak", out).ok() && !out.isEmpty();
}

// One atomic replacement. On Windows the rename can fail for a moment while another process has the file open,
// so a few jittered retries are part of a write, not an error.
bool replaceFile(const QString& path, const QByteArray& bytes, bool retry) {
    for (int attempt = 0; attempt < (retry ? 10 : 1); ++attempt) {
        QSaveFile output(path);
        if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit()) return true;
        // Jittered: timer granularity can otherwise phase-lock a retry with a periodic foreign reader.
        if (retry) QThread::msleep(quint32(10 + 15 * attempt + QRandomGenerator::global()->bounded(20)));
    }
    return false;
}
}

QString settingsDirectory() {
    const QString override = qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : override;
}
QString settingsFilePath() { return QDir(settingsDirectory()).filePath("settings.json"); }

SettingsStatus readSettings(QJsonObject& out) {
    const QString path = settingsFilePath();
    // Deliberately lock-free: readers must never queue behind (or starve) writers; a replacement in flight is
    // handled by the bounded retry below and by the writer's own retry.
    SettingsStatus status;
    // Writers replace the file atomically, but on Windows an open can fail for a moment while the replacement
    // commits; retry only that case (a damaged file is not transient and must not stall startup).
    for (int attempt = 0; attempt < 8; ++attempt) {
        status = parseExisting(path, out);
        // A schema-1 file is never empty, so ok-and-empty means "absent". Once a writer has used this directory
        // (its lock file exists) absence can be the instant of a concurrent replacement on Linux; look again.
        const bool vanished = status.ok() && out.isEmpty() && QFileInfo::exists(path + ".lock");
        // A genuinely absent file (e.g. after a reset) must not cost the UI thread the full unreadable budget.
        if (status.code != SettingsStatus::Unreadable && !(vanished && attempt < 2)) break;
        QThread::msleep(10);
    }
    if (status.code == SettingsStatus::Corrupt) {
        QJsonObject backup;
        if (validBackup(path, backup)) { out = backup; return {SettingsStatus::Ok, true}; }
    }
    return status;
}

namespace settings_detail {
Result transact(const Request& input,const Editor& edit,const PreimageWriter& preimageWriter) {
    using namespace recovery;
    const Request request=input; // retain private namespace ownership and options
    Result result;result.status.generation=request.options.generation;
    const bool ordinary=request.policy==Policy::Ordinary;
    auto finish=[&](Code code,QString detail={}) {
        result.status.code=code;result.status.detail=std::move(detail);return result;
    };
    auto cancelled=[&]{return request.options.cancellation.stop_requested();};
    std::function<void(Stage)> observer;
    std::function<bool(Stage)> injectFailure;
#ifdef GLANCELOT_RECOVERY_TESTING
    if(transactionHooks){observer=transactionHooks->observe;injectFailure=transactionHooks->fail;}
#endif
    auto stage=[&](Stage point) {
        if(observer)observer(point);
        return injectFailure&&injectFailure(point);
    };
    if(cancelled())return finish(Code::Cancelled);
    QString directory,namespaceId;
    if(ordinary)directory=QDir::cleanPath(QFileInfo(request.context.directory).absoluteFilePath());
    else {
        const auto resolved=raw::resolve(request.context,request.options,directory,namespaceId);
        if(resolved.code!=Code::Ok)return finish(resolved.code,resolved.detail);
    }
    const auto path=QDir(directory).filePath("settings.json");
    if(!QDir().mkpath(directory))return finish(Code::WriteFailed);
    FileLock lock(path+".lock");
    if(ordinary) {
        if(!lock.acquire(request.options.lockTimeoutMs))return finish(lock.contended()?Code::Busy:Code::WriteFailed);
    }else {
        QElapsedTimer elapsed;elapsed.start();
        while(!lock.acquire(0)) {
            if(cancelled())return finish(Code::Cancelled);
            if(!lock.contended()||elapsed.elapsed()>=request.options.lockTimeoutMs)
                return finish(lock.contended()?Code::Busy:Code::WriteFailed);
            QThread::msleep(5);
        }
    }
    View view;QByteArray primaryBytes;bool existed{},primaryGood{};
    if(ordinary) {
        auto parsed=parseExisting(path,view.base,&primaryBytes);
        primaryGood=parsed.ok();existed=QFileInfo::exists(path);
        if(parsed.code==SettingsStatus::Corrupt&&validBackup(path,view.base)) {
            view.recovered=true;primaryGood=false;parsed={};
        }
        if(!parsed.ok()) {
            switch(parsed.code) {
            case SettingsStatus::Unsupported:return finish(Code::Unsupported);
            case SettingsStatus::Corrupt:return finish(Code::Corrupt);
            default:return finish(Code::Unreadable);
            }
        }
    }else {
        auto snapshot=raw::readLocked(directory,namespaceId,request.options);
        if(snapshot.status.code!=Code::Ok)return finish(snapshot.status.code,snapshot.status.detail);
        view.raw=std::move(snapshot.snapshot);result.revision=view.raw->revision;
        if((!request.expectedNamespace.isEmpty()&&request.expectedNamespace!=namespaceId)||
           (!request.expectedRevision.isEmpty()&&request.expectedRevision!=view.raw->revision))return finish(Code::Conflict);
        if(request.policy==Policy::Restore&&
           (request.expectedNamespace.isEmpty()||request.expectedRevision.isEmpty()||!preimageWriter))
            return finish(Code::Corrupt,QStringLiteral("Restore requires a checked revision, namespace and preimage writer."));
        const auto& primary=view.raw->primary;const auto& backup=view.raw->lastGood;
        if(primary.kind==ParseKind::Unsupported)return finish(Code::Unsupported);
        existed=primary.present;primaryGood=!existed||primary.kind==ParseKind::Supported;
        primaryBytes=primary.bytes;
        if(primary.kind==ParseKind::Corrupt) {
            if(backup.kind!=ParseKind::Supported)return finish(Code::Corrupt);
            view.base=QJsonDocument::fromJson(backup.bytes).object();view.recovered=true;
        }else if(primary.present)view.base=QJsonDocument::fromJson(primary.bytes).object();
    }
    result.recovered=view.recovered;
    if(stage(Stage::AfterRead))return finish(Code::WriteFailed);
    if(cancelled())return finish(Code::Cancelled);
    if(!edit)return finish(Code::Corrupt);
    auto decision=edit(view);
    // Compare raw revisions BEFORE the editor/no-change path. No abort/no-op
    // editor can accidentally rewrite backup or damaged evidence.
    if(cancelled())return finish(Code::Cancelled);
    if(decision.decision!=Code::Committed) {
        if(decision.decision==Code::Ok)return finish(Code::Corrupt);
        return finish(decision.decision);
    }
    decision.replacement["schema"]=1;
    const auto bytes=QJsonDocument(decision.replacement).toJson(QJsonDocument::Indented);
    if(bytes.size()>kMaxSettingsBytes)return finish(ordinary?Code::WriteFailed:Code::LimitExceeded);
    if(stage(Stage::BeforeEvidence))return finish(Code::WriteFailed);
    if(cancelled())return finish(Code::Cancelled);
    if(request.policy==Policy::Restore) {
        const auto sealed=preimageWriter(*view.raw);result.preimage=sealed.path;
        if(sealed.status.code!=Code::Ok) {
            const auto code=sealed.status.code==Code::Committed||sealed.status.code==Code::NoChange?
                Code::WriteFailed:sealed.status.code;
            return finish(code,sealed.status.detail);
        }
        if(sealed.path.isEmpty())return finish(Code::WriteFailed,QStringLiteral("Preimage was not sealed."));
        if(stage(Stage::AfterPreimage))return finish(Code::WriteFailed);
        if(cancelled())return finish(Code::Cancelled);
    }else {
        if(existed&&primaryGood&&!replaceFile(path+".bak",primaryBytes,request.retryReplacements))return finish(Code::WriteFailed);
        if(view.recovered&&!replaceFile(path+".damaged",primaryBytes,request.retryReplacements))return finish(Code::WriteFailed);
    }
    if(stage(Stage::BeforeReplace))return finish(Code::WriteFailed);
    if(cancelled())return finish(Code::Cancelled);
    if(!replaceFile(path,bytes,request.retryReplacements))return finish(Code::WriteFailed);
    if(view.raw) {
        const auto backup=request.policy==Policy::Restore?view.raw->lastGood:
            existed&&primaryGood?raw::classify(primaryBytes):view.raw->lastGood;
        result.revision=raw::revision(raw::classify(bytes),backup);
    }
    stage(Stage::AfterReplace); // commit already happened: no cancel/failure rollback claim
    return finish(Code::Committed);
}
}

SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit,bool waitOnExit) {
    const bool retry=waitOnExit||!interactiveThread();
    settings_detail::Request request;request.context.directory=settingsDirectory();
    request.policy=settings_detail::Policy::Ordinary;request.options.lockTimeoutMs=retry?4000:0;
    request.retryReplacements=retry;
    const auto result=settings_detail::transact(request,[&](const settings_detail::View& view) {
        auto replacement=view.base;edit(replacement);
        return settings_detail::Edit{recovery::Code::Committed,std::move(replacement)};
    });
    switch(result.status.code) {
    case recovery::Code::Committed:case recovery::Code::NoChange:return {SettingsStatus::Ok,result.recovered};
    case recovery::Code::Unreadable:return {SettingsStatus::Unreadable};
    case recovery::Code::Unsupported:return {SettingsStatus::Unsupported};
    case recovery::Code::Corrupt:return {SettingsStatus::Corrupt};
    case recovery::Code::Busy:return {SettingsStatus::Busy};
    default:return {SettingsStatus::WriteFailed};
    }
}

bool resetSettings(QString* movedTo) {
    const QString path = settingsFilePath();
    if (!QFileInfo::exists(path)) return true;
    FileLock lock(path + ".lock");
    if (!lock.acquire(interactiveThread() ? 0 : 4000)) return false;
    const QString target = path + ".reset-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    if (movedTo) *movedTo = target;
    return QFile::rename(path, target);
}

bool exportSettings(const QString& targetFilePath, QString* error) {
    if (targetFilePath.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Target file path cannot be empty.");
        return false;
    }
    QJsonObject root;
    const auto status = readSettings(root);
    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not read settings for export.");
        return false;
    }
    QSaveFile saveFile(targetFilePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not create target export file.");
        return false;
    }
    root["schema"] = 1;
    saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!saveFile.commit()) {
        if (error) *error = QStringLiteral("Could not write export file atomically.");
        return false;
    }
    return true;
}

bool importSettings(const QString& sourceFilePath, QString* error) {
    QFile file(sourceFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open settings file for import.");
        return false;
    }
    const auto bytes = file.readAll();
    if (bytes.size() > kMaxSettingsBytes) {
        if (error) *error = QStringLiteral("Import file exceeds maximum settings size.");
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("Import file contains invalid JSON.");
        return false;
    }
    const auto imported = doc.object();
    const int schema = imported.value(QStringLiteral("schema")).toInt(1);
    if (schema > 1) {
        if (error) *error = QStringLiteral("Import file has an unsupported newer schema.");
        return false;
    }

    const auto status = updateSettings([&](QJsonObject& root) {
        for (auto it = imported.begin(); it != imported.end(); ++it) {
            if (it.key() == QStringLiteral("schema")) continue;
            root[it.key()] = it.value();
        }
    });

    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not save imported settings.");
        return false;
    }
    return true;
}

bool resetSection(const QString& sectionName, QString* error) {
    const QString sec = sectionName.trimmed().toLower();
    const auto status = updateSettings([&](QJsonObject& root) {
        if (sec == QStringLiteral("appearance")) {
            root.remove(QStringLiteral("appearance"));
            root.remove(QStringLiteral("textScale"));
            root.remove(QStringLiteral("reducedMotion"));
        } else if (sec == QStringLiteral("viewing")) {
            root.remove(QStringLiteral("boundary"));
            root.remove(QStringLiteral("wheel"));
            root.remove(QStringLiteral("nearest"));
            root.remove(QStringLiteral("backdrop"));
        } else if (sec == QStringLiteral("browsing")) {
            root.remove(QStringLiteral("filmstrip"));
            root.remove(QStringLiteral("tree"));
            root.remove(QStringLiteral("treePinned"));
            root.remove(QStringLiteral("sort"));
        } else if (sec == QStringLiteral("panes")) {
            root.remove(QStringLiteral("layouts"));
        } else if (sec == QStringLiteral("bindings")) {
            root.remove(QStringLiteral("bindings"));
        } else if (sec == QStringLiteral("all")) {
            root.remove(QStringLiteral("appearance"));
            root.remove(QStringLiteral("textScale"));
            root.remove(QStringLiteral("reducedMotion"));
            root.remove(QStringLiteral("boundary"));
            root.remove(QStringLiteral("wheel"));
            root.remove(QStringLiteral("nearest"));
            root.remove(QStringLiteral("backdrop"));
            root.remove(QStringLiteral("filmstrip"));
            root.remove(QStringLiteral("tree"));
            root.remove(QStringLiteral("treePinned"));
            root.remove(QStringLiteral("sort"));
            root.remove(QStringLiteral("layouts"));
            root.remove(QStringLiteral("bindings"));
        }
    });

    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not reset settings section.");
        return false;
    }
    return true;
}
}

