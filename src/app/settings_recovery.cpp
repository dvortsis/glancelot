#include "glancelot/settings_recovery.hpp"
#include "settings_raw.hpp"
#include "file_lock.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
}
