#include "glancelot/settings_recovery.hpp"
#include "settings_raw.hpp"
#include "settings_package.hpp"
#include "file_lock.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QThread>
#include <filesystem>

namespace glancelot::recovery {
namespace {
namespace fs=std::filesystem;
fs::path native(const QString& value) {
#ifdef Q_OS_WIN
    return fs::path(value.toStdWString());
#else
    return fs::path(QFile::encodeName(value).constData());
#endif
}
QString display(const fs::path& value) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(value.native());
#else
    return QFile::decodeName(value.native().c_str());
#endif
}
void appendLength(QByteArray& bytes,quint64 value) {
    for(int shift=0;shift<64;shift+=8)bytes.append(char((value>>shift)&0xff));
}
QByteArray digest(const QByteArray& value) {return QCryptographicHash::hash(value,QCryptographicHash::Sha256);}
void appendMember(QByteArray& bytes,const RawMember& member) {
    bytes.append(member.present?char(1):char(0));
    appendLength(bytes,member.present?quint64(member.bytes.size()):0);
    bytes.append(member.present?member.sha256:QByteArray(32,char(0)));
}
Status status(Code code,Options options,QString detail={}) {return {code,std::move(detail),options.generation};}
Status readMember(const QString& path,RawMember& result,Options options) {
    if(options.cancellation.stop_requested())return status(Code::Cancelled,options);
    std::error_code error;
    const auto kind=fs::symlink_status(native(path),error);
    if(error==std::errc::no_such_file_or_directory||(!error&&kind.type()==fs::file_type::not_found)) {
        result={};return status(Code::Ok,options);
    }
    if(error||kind.type()!=fs::file_type::regular)
        return status(Code::Unreadable,options,QStringLiteral("Settings member is not a readable regular file."));
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return status(Code::Unreadable,options,file.errorString());
    if(file.size()>MaxMemberBytes)return status(Code::LimitExceeded,options,QStringLiteral("Settings member exceeds 1 MiB."));
    auto bytes=file.read(MaxMemberBytes+1);
    if(file.error()!=QFileDevice::NoError)return status(Code::Unreadable,options,file.errorString());
    if(bytes.size()>MaxMemberBytes)return status(Code::LimitExceeded,options,QStringLiteral("Settings member exceeds 1 MiB."));
    if(options.cancellation.stop_requested())return status(Code::Cancelled,options);
    result=raw::classify(std::move(bytes));return status(Code::Ok,options);
}
}
namespace raw {
Status resolve(const Context& context,Options options,QString& directory,QString& namespaceId) {
    if(options.cancellation.stop_requested())return status(Code::Cancelled,options);
    if(options.lockTimeoutMs<0||options.lockTimeoutMs>MaxLockWaitMs)
        return status(Code::LimitExceeded,options,QStringLiteral("Invalid lock timeout."));
    if(context.directory.isEmpty()||context.directory.contains(QChar(0))||!QDir::isAbsolutePath(context.directory))
        return status(Code::Unreadable,options,QStringLiteral("Recovery requires an absolute configuration directory."));
    std::error_code error;
    const auto resolved=fs::weakly_canonical(native(context.directory),error);
    if(error)return status(Code::Unreadable,options,QStringLiteral("Cannot resolve configuration namespace."));
    directory=QDir::cleanPath(QDir::fromNativeSeparators(display(resolved)));
    auto identity=directory;
#ifdef Q_OS_WIN
    identity=identity.toCaseFolded();
#endif
    const auto utf8=identity.toUtf8();
    QByteArray encoded("Glancelot.SettingsNamespace");encoded.append(char(0));encoded.append(char(1));
    appendLength(encoded,quint64(utf8.size()));encoded.append(utf8);
    namespaceId=QStringLiteral("ns1:")+QString::fromLatin1(digest(encoded).toHex());
    return status(Code::Ok,options);
}
RawMember classify(QByteArray bytes) {
    RawMember result;result.present=true;result.bytes=std::move(bytes);result.sha256=digest(result.bytes);
    QJsonParseError error;const auto parsed=QJsonDocument::fromJson(result.bytes,&error);
    if(error.error!=QJsonParseError::NoError||!parsed.isObject())result.kind=ParseKind::Corrupt;
    else {
        const auto schema=parsed.object().value(QStringLiteral("schema"));
        result.kind=schema.isDouble()&&schema.toDouble()==1?ParseKind::Supported:ParseKind::Unsupported;
    }
    return result;
}
QString revision(const RawMember& primary,const RawMember& backup) {
    QByteArray encoded("Glancelot.SettingsPair");encoded.append(char(0));encoded.append(char(1));
    appendMember(encoded,primary);appendMember(encoded,backup);
    return QStringLiteral("sr1:")+QString::fromLatin1(digest(encoded).toHex());
}
SnapshotResult readLocked(const QString& directory,const QString& namespaceId,Options options) {
    RawSnapshot result;result.directory=directory;result.namespaceId=namespaceId;
    auto read=readMember(QDir(directory).filePath(QStringLiteral("settings.json")),result.primary,options);
    if(read.code!=Code::Ok)return {std::move(read),std::nullopt};
    read=readMember(QDir(directory).filePath(QStringLiteral("settings.json.bak")),result.lastGood,options);
    if(read.code!=Code::Ok)return {std::move(read),std::nullopt};
    result.revision=revision(result.primary,result.lastGood);
    return {status(Code::Ok,options),std::move(result)};
}
}
SnapshotResult readRaw(const Context& input,Options options) {
    // Keep private-root ownership until the last filesystem operation returns.
    const Context context=input;
    QString directory,namespaceId;auto resolved=raw::resolve(context,options,directory,namespaceId);
    if(resolved.code!=Code::Ok)return {std::move(resolved),std::nullopt};
    if(!QDir().mkpath(directory))return {status(Code::Unreadable,options,QStringLiteral("Cannot access configuration directory.")),std::nullopt};
    FileLock lock(QDir(directory).filePath(QStringLiteral("settings.json.lock")));
    QElapsedTimer elapsed;elapsed.start();
    while(!lock.acquire(0)) {
        if(options.cancellation.stop_requested())return {status(Code::Cancelled,options),std::nullopt};
        if(!lock.contended()||elapsed.elapsed()>=options.lockTimeoutMs)
            return {status(lock.contended()?Code::Busy:Code::Unreadable,options),std::nullopt};
        QThread::msleep(5);
    }
    return raw::readLocked(directory,namespaceId,options);
}
namespace package {
namespace {
QString revision(const QByteArray& manifest,const RawMember& primary,const RawMember& backup) {
    QByteArray bytes("Glancelot.SettingsPackage");bytes.append(char(0));bytes.append(char(1));
    appendLength(bytes,quint64(manifest.size()));bytes.append(digest(manifest));
    appendMember(bytes,primary);appendMember(bytes,backup);
    return QStringLiteral("sp1:")+QString::fromLatin1(digest(bytes).toHex());
}
QJsonObject descriptor(const QString& name,const RawMember& member) {
    return {{"name",name},{"present",member.present},{"bytes",member.present?double(member.bytes.size()):0.0},
            {"sha256",QString::fromLatin1(member.present?member.sha256.toHex():QByteArray(64,'0'))}};
}
bool validMember(const RawMember& member) {
    return member.present?member.bytes.size()<=MaxMemberBytes&&member.sha256==digest(member.bytes):
        member.bytes.isEmpty()&&member.sha256.isEmpty();
}
Status readRegular(const QString& path,qint64 limit,QByteArray& bytes,Options options) {
    std::error_code error;const auto type=fs::symlink_status(native(path),error);
    if(error||type.type()!=fs::file_type::regular)return status(Code::Corrupt,options,QStringLiteral("Missing or nonregular package member."));
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return status(Code::Unreadable,options,file.errorString());
    if(file.size()>limit)return status(Code::LimitExceeded,options);
    bytes=file.read(limit+1);
    if(file.error()!=QFileDevice::NoError)return status(Code::Unreadable,options,file.errorString());
    if(bytes.size()>limit)return status(Code::LimitExceeded,options);
    return status(options.cancellation.stop_requested()?Code::Cancelled:Code::Ok,options);
}
}
CaptureResult write(const RawSnapshot& snapshot,const QString& destination,Options options) {
    CaptureResult result;result.status=status(Code::Corrupt,options);
    auto finish=[&](Code code,QString detail={}){result.status=status(code,options,std::move(detail));return result;};
    if(options.cancellation.stop_requested())return finish(Code::Cancelled);
    if(destination.isEmpty()||destination.contains(QChar(0))||!QDir::isAbsolutePath(destination))return finish(Code::Unreadable);
    if(!validMember(snapshot.primary)||!validMember(snapshot.lastGood))return finish(Code::Corrupt);
    const auto target=native(QDir::cleanPath(destination));std::error_code error;
    if(!fs::create_directory(target,error))return finish(error&&error!=std::errc::file_exists?Code::WriteFailed:Code::Conflict,
        QStringLiteral("Backup destination must be a new directory."));
    result.destination=display(target);
    std::function<void(Stage)> observe;std::function<bool(Stage)> fail;
#ifdef GLANCELOT_RECOVERY_TESTING
    if(hooks){observe=hooks->observe;fail=hooks->fail;}
#endif
    auto stage=[&](Stage point){if(observe)observe(point);return fail&&fail(point);};
    if(stage(Stage::DirectoryCreated))return finish(Code::WriteFailed);
    const QDir directory(result.destination);
    auto writeMember=[&](const char* name,const RawMember& member) {
        if(options.cancellation.stop_requested())return Code::Cancelled;
        if(!member.present)return Code::Ok;
        QFile file(directory.filePath(QLatin1String(name)));
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(member.bytes)!=member.bytes.size()||!file.flush())return Code::WriteFailed;
        file.close();return file.error()==QFileDevice::NoError?Code::Ok:Code::WriteFailed;
    };
    auto written=writeMember("settings.json",snapshot.primary);if(written!=Code::Ok)return finish(written);
    if(stage(Stage::PrimaryWritten))return finish(Code::WriteFailed);
    written=writeMember("settings.json.bak",snapshot.lastGood);if(written!=Code::Ok)return finish(written);
    if(stage(Stage::BackupWritten))return finish(Code::WriteFailed);
    const auto manifest=QJsonDocument(QJsonObject{{"schema",1},{"sealed",true},{"members",QJsonArray{
        descriptor("settings.json",snapshot.primary),descriptor("settings.json.bak",snapshot.lastGood)}}}).toJson(QJsonDocument::Compact);
    if(manifest.size()>MaxManifestBytes||manifest.size()+snapshot.primary.bytes.size()+snapshot.lastGood.bytes.size()>MaxPackageBytes)
        return finish(Code::LimitExceeded);
    if(stage(Stage::BeforeSeal))return finish(Code::WriteFailed);
    if(options.cancellation.stop_requested())return finish(Code::Cancelled);
    // Stage inside the exclusively created directory, then publish by a rename
    // that refuses an existing final name. Partial packages have no manifest.
    const auto staging=directory.filePath(".manifest-seal");
    QFile seal(staging);
    if(!seal.open(QIODevice::WriteOnly|QIODevice::NewOnly)||seal.write(manifest)!=manifest.size()||!seal.flush())return finish(Code::WriteFailed);
    seal.close();if(seal.error()!=QFileDevice::NoError)return finish(Code::WriteFailed);
    if(options.cancellation.stop_requested())return finish(Code::Cancelled);
    if(!QFile::rename(staging,directory.filePath("manifest.json")))return finish(Code::WriteFailed);
    result.packageRevision=revision(manifest,snapshot.primary,snapshot.lastGood);
    stage(Stage::AfterSeal); // publication happened; cancellation/fault is too late
    return finish(Code::Ok);
}
ReadResult read(const QString& directory,Options options) {
    auto failed=[&](Code code,QString detail={}){return ReadResult{status(code,options,std::move(detail)),std::nullopt};};
    if(options.cancellation.stop_requested())return failed(Code::Cancelled);
    if(directory.isEmpty()||directory.contains(QChar(0))||!QDir::isAbsolutePath(directory))return failed(Code::Unreadable);
    std::error_code error;const auto root=native(directory);
    if(fs::symlink_status(root,error).type()!=fs::file_type::directory||error)return failed(Code::Corrupt);
    QSet<QString> names;
    fs::directory_iterator it(root,error),end;
    if(error)return failed(Code::Unreadable);
    for(;it!=end;it.increment(error)) {
        if(error)return failed(Code::Unreadable);
        if(options.cancellation.stop_requested())return failed(Code::Cancelled);
        if(names.size()>=3)return failed(Code::Corrupt,QStringLiteral("Unexpected package entries."));
        const auto name=display(it->path().filename());
        if(name!="manifest.json"&&name!="settings.json"&&name!="settings.json.bak")return failed(Code::Corrupt);
        if(names.contains(name))return failed(Code::Corrupt);
        names.insert(name);
    }
    if(error)return failed(Code::Unreadable);
    Data data;const QDir dir(directory);
    auto manifestRead=readRegular(dir.filePath("manifest.json"),MaxManifestBytes,data.manifest,options);
    if(manifestRead.code!=Code::Ok)return {manifestRead,std::nullopt};
    QJsonParseError parseError;const auto document=QJsonDocument::fromJson(data.manifest,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject())return failed(Code::Corrupt);
    const auto object=document.object();const auto schema=object.value("schema");
    if(!schema.isDouble()||schema.toDouble()!=1)return failed(Code::Unsupported);
    if(object.value("sealed")!=QJsonValue(true)||!object.value("members").isArray())return failed(Code::Corrupt);
    const auto members=object.value("members").toArray();if(members.size()!=2)return failed(Code::Corrupt);
    qint64 aggregate=data.manifest.size();
    for(int index=0;index<2;++index) {
        const QString name=index==0?QStringLiteral("settings.json"):QStringLiteral("settings.json.bak");
        if(!members[index].isObject())return failed(Code::Corrupt);
        const auto member=members[index].toObject();
        if(member.value("name")!=QJsonValue(name)||!member.value("present").isBool()||!member.value("bytes").isDouble()||!member.value("sha256").isString())return failed(Code::Corrupt);
        const auto length=member.value("bytes").toDouble(-1);const auto hex=member.value("sha256").toString();
        if(length<0||length>MaxMemberBytes||double(qint64(length))!=length)return failed(length>MaxMemberBytes?Code::LimitExceeded:Code::Corrupt);
        if(hex.size()!=64)return failed(Code::Corrupt);
        for(const auto ch:hex)if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))return failed(Code::Corrupt);
        auto& value=index==0?data.primary:data.lastGood;
        const bool present=member.value("present").toBool();
        if(!present) {
            if(length!=0||hex!=QString(64,'0')||names.contains(name))return failed(Code::Corrupt);
            continue;
        }
        QByteArray bytes;const auto loaded=readRegular(dir.filePath(name),MaxMemberBytes,bytes,options);
        if(loaded.code!=Code::Ok)return {loaded,std::nullopt};
        aggregate+=bytes.size();if(aggregate>MaxPackageBytes)return failed(Code::LimitExceeded);
        if(bytes.size()!=qint64(length)||digest(bytes).toHex()!=hex.toLatin1())return failed(Code::Corrupt);
        value=raw::classify(std::move(bytes));
    }
    data.revision=revision(data.manifest,data.primary,data.lastGood);
    return {status(Code::Ok,options),std::move(data)};
}
}
CaptureResult captureBackup(const Context& input,const QString& destination,Options options) {
    const Context context=input;
    auto source=readRaw(context,options);if(source.status.code!=Code::Ok)return {source.status,{}, {}};
    return package::write(*source.snapshot,destination,options);
}
}
