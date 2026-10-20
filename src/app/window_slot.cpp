#include "glancelot/window_slot.hpp"
#include "glancelot/settings_file.hpp"
#include "file_lock.hpp"
#include "window_claim_guard.hpp"
#include "settings_raw.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QUuid>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <filesystem>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

namespace {
constexpr int kMaxCounter = 1000000;

// A value that identifies one run of a process: 0 when it is not alive. Pid reuse produces a different stamp.
qint64 processStamp(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!handle) return GetLastError()==ERROR_INVALID_PARAMETER?0:1;
    DWORD code = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    qint64 stamp = 1; // inaccessible identity is ambiguous, never proof of death
    if(GetExitCodeProcess(handle,&code)&&code!=STILL_ACTIVE){CloseHandle(handle);return 0;}
    if (GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE && GetProcessTimes(handle, &created, &exited, &kernel, &user))
        stamp = (qint64(created.dwHighDateTime) << 32 | created.dwLowDateTime) / 10000; // milliseconds since 1601
    CloseHandle(handle);
    return stamp;
#else
    if (::kill(pid_t(pid), 0) != 0 && errno != EPERM) return 0;
    QFile stat(QString("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return 1; // alive but no /proc: identity cannot be refined
    const QByteArray line = stat.readAll();
    const int close = line.lastIndexOf(')');
    if (close < 0 || close + 2 >= line.size()) return 1;
    if (line.at(close + 2) == 'Z') return 0;       // a crashed, not yet reaped child is not a live window
    const QList<QByteArray> fields = line.mid(close + 2).split(' ');
    return fields.size() > 19 ? fields.at(19).toLongLong() + 2 : 1; // starttime (field 22), never 0 or 1
#endif
}

struct Session {
    QJsonObject extra;   // unknown keys of a schema-1 file are carried through untouched
    int counter{};
    QJsonArray live;
};
enum class Loaded { Ok, Refuse };

QString statePath(const QString& directory) { return QDir(directory).filePath("sessions.json"); }

// Only a missing file starts empty. Unreadable, damaged or oversized bookkeeping cannot
// prove that existing slots are free, so it must never be rewritten by a new claimant.
Loaded load(const QString& directory, Session& session) {
    session = {};
    QFile file(statePath(directory));
    if (!file.exists()) return Loaded::Ok;
    if (!file.open(QIODevice::ReadOnly)) return Loaded::Refuse;
    if (file.size() > 64 * 1024) return Loaded::Refuse;
    const auto bytes=file.read(64*1024+1);
    if(file.error()!=QFileDevice::NoError) return Loaded::Refuse;
    if(bytes.size()>64*1024) return Loaded::Refuse;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return Loaded::Refuse;
    const auto root = document.object();
    if (root.value("schema").toInt() != 1 || !root.value("live").isArray()
        || !root.value("counter").isDouble()) return Loaded::Refuse;
    session.extra = root;
    session.counter = std::clamp(root.value("counter").toInt(), 0, kMaxCounter);
    session.live = root.value("live").toArray();
    return Loaded::Ok;
}

bool store(const QString& directory, const Session& session, bool retry) {
    QJsonObject root = session.extra;
    root["schema"] = 1;
    root["counter"] = session.counter;
    root["live"] = session.live;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if(bytes.size()>64*1024) return false; // never exceed the coordination reader's own bound
    for (int attempt = 0; attempt < (retry ? 8 : 1); ++attempt) {
        QSaveFile file(statePath(directory));
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
        if (retry) QThread::msleep(quint32(8 + 7 * attempt));
    }
    return false;
}

// Runs `body(session)` under the cross-process lock; false when the lock, the file's schema or the write refused.
template <typename Body>
bool locked(const QString& directory, int timeoutMs, Body body) {
    if (!QDir().mkpath(directory)) return false;
    FileLock lock(statePath(directory) + ".lock");
    if (!lock.acquire(timeoutMs)) return false;
    Session session;
    if (load(directory, session) == Loaded::Refuse) return false;
    if (!body(session)) return false;
    return store(directory, session, timeoutMs > 0);
}
}

WindowSlot::WindowSlot(const QString& directory,int timeoutMs):WindowSlot(directory,timeoutMs,{}) {}
WindowSlot::WindowSlot(const QString& directory, int timeoutMs,std::shared_ptr<const void> lifetime) : directory_(directory.isEmpty() ? glancelot::settingsDirectory() : directory),lifetime_(std::move(lifetime)) {
    directory_=QDir(directory_).absolutePath();
    claim(timeoutMs);
}

bool WindowSlot::claim(int timeoutMs) {
    if (number_ != 0) return true;
    const qint64 pid = QCoreApplication::applicationPid();
    const qint64 stamp = std::max<qint64>(1, processStamp(pid));
    const auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
    int claimed = 0;
    const bool ok = locked(directory_, timeoutMs, [&](Session& session) {
        QJsonArray alive;
        int highestLiveSlot=0;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            const qint64 stampNow = processStamp(object.value("pid").toInteger());
            const int liveSlot=object.value("slot").toInt();
            if (stampNow != 0 && (stampNow==1||object.value("stamp").toInteger()==1||stampNow == object.value("stamp").toInteger()) && liveSlot>0 && liveSlot<=kMaxCounter) {
                alive.append(object);
                highestLiveSlot=std::max(highestLiveSlot,liveSlot);
            }
        }
        session.live = alive;
        if (session.live.isEmpty()) session.counter = 0; // a new session
        else session.counter=std::max(session.counter,highestLiveSlot);
        if (session.counter >= kMaxCounter) return false; // never reuse a live session's slots
        claimed = ++session.counter;
        session.live.append(QJsonObject{{"slot", claimed}, {"pid", pid}, {"stamp", stamp},{"claimNonce",nonce}});
        return true;
    });
    number_ = ok ? claimed : 0;
    if(ok&&stamp>1) {
        QString directory,namespaceId;
        glancelot::recovery::Options options;options.lockTimeoutMs=0;
        if(glancelot::recovery::raw::resolve({directory_,lifetime_},options,directory,namespaceId).code==glancelot::recovery::Code::Ok) {
            auto state=std::make_shared<glancelot::detail::WindowClaimState>();
            state->directory=directory;state->namespaceId=namespaceId;state->slot=claimed;
            state->pid=pid;state->stamp=stamp;state->nonce=nonce;state->lifetime=lifetime_;claimState_=std::move(state);
        }
    }
    return number_ != 0;
}

void WindowSlot::release() {
    if (number_ == 0) return;
    const qint64 pid = QCoreApplication::applicationPid();
    const int slot = number_;
    const auto nonce=claimState_?claimState_->nonce:QString{};
    if(claimState_)claimState_->active=false;
    claimState_.reset();
    number_ = 0;
    locked(directory_, 3000, [&](Session& session) {
        QJsonArray remaining;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            if (!(object.value("pid").toInteger() == pid && object.value("slot").toInt() == slot&&
                  (nonce.isEmpty()||object.value("claimNonce").toString()==nonce))) remaining.append(object);
        }
        session.live = remaining;
        if (session.live.isEmpty()) session.counter = 0;
        return true;
    });
}

WindowSlot::~WindowSlot() { release(); }

glancelot::WindowClaim WindowSlot::claimIdentity() const {
    glancelot::WindowClaim result;
    if(claimState_&&claimState_->active.load())result.state_=claimState_;
    return result;
}

namespace glancelot::recovery::ownership {
namespace {
Loaded loadForRecovery(const QString& directory,Session& session) {
    // QFile::exists follows links; a dangling bookkeeping link must not be
    // reinterpreted as absent coordination state and authorize offline restore.
    std::error_code error;
#ifdef Q_OS_WIN
    const auto native=std::filesystem::path(statePath(directory).toStdWString());
#else
    const auto native=std::filesystem::path(QFile::encodeName(statePath(directory)).constData());
#endif
    const auto kind=std::filesystem::symlink_status(native,error);
    if(error==std::errc::no_such_file_or_directory||(!error&&kind.type()==std::filesystem::file_type::not_found)) {session={};return Loaded::Ok;}
    if(error||kind.type()!=std::filesystem::file_type::regular)return Loaded::Refuse;
    return load(directory,session);
}
Guard acquireBase(const Context& context,Options options) {
    Guard result;result.status.generation=options.generation;result.lifetime=context.lifetime;
    const auto resolved=raw::resolve(context,options,result.directory,result.namespaceId);
    if(resolved.code!=Code::Ok){result.status=resolved;return result;}
    if(!QDir().mkpath(result.directory)){result.status.code=Code::Unreadable;return result;}
    result.sessionsLock=std::make_unique<FileLock>(statePath(result.directory)+".lock");
    QElapsedTimer timer;timer.start();
    while(!result.sessionsLock->acquire(0)) {
        if(options.cancellation.stop_requested()){result.status.code=Code::Cancelled;result.sessionsLock.reset();return result;}
        if(!result.sessionsLock->contended()||timer.elapsed()>=options.lockTimeoutMs) {
            result.status.code=result.sessionsLock->contended()?Code::Busy:Code::Unreadable;
            result.sessionsLock.reset();return result;
        }
        QThread::msleep(5);
    }
    if(options.cancellation.stop_requested()){result.status.code=Code::Cancelled;result.sessionsLock.reset();return result;}
    result.status.code=Code::Ok;return result;
}
bool integer(const QJsonValue& value,qint64 low,qint64 high) {
    const double number=value.toDouble(-1);
    return value.isDouble()&&std::isfinite(number)&&number>=double(low)&&number<=double(high)&&std::floor(number)==number;
}
bool validSession(const Session& session) {
    if(session.extra.isEmpty())return true; // only a genuinely missing file
    if(session.extra.value("schema")!=QJsonValue(1)||!integer(session.extra.value("counter"),0,kMaxCounter))return false;
    QSet<int> seenSlots;
    for(const auto& value:session.live) {
        if(!value.isObject())return false;const auto row=value.toObject();
        if(!integer(row.value("slot"),1,kMaxCounter)||!integer(row.value("pid"),1,2147483647)||
           !integer(row.value("stamp"),1,9007199254740991LL))return false;
        const auto slot=row.value("slot").toInt();if(seenSlots.contains(slot))return false;seenSlots.insert(slot);
        if(row.contains("claimNonce")) {
            const auto text=row.value("claimNonce").toString();const QUuid id(text);
            if(!row.value("claimNonce").isString()||id.isNull()||id.toString(QUuid::WithoutBraces)!=text)return false;
        }
    }
    return true;
}
Guard refuse(Guard result,Code code) {result.status.code=code;result.sessionsLock.reset();return result;}
}
Guard acquire(const WindowClaim& identity,const Context& context,Options options) {
    const auto claim=glancelot::detail::WindowClaimAccess::get(identity);
    if(!claim||!claim->active.load()||claim->stamp<=1) {Guard result;result.status={Code::Conflict,{},options.generation};return result;}
    auto result=acquireBase(context,options);if(!result.held())return result;
    if(!claim->active.load()||claim->namespaceId!=result.namespaceId)return refuse(std::move(result),Code::Conflict);
    Session session;if(loadForRecovery(result.directory,session)!=Loaded::Ok||!validSession(session))return refuse(std::move(result),Code::Corrupt);
    int matches=0;
    for(const auto& value:session.live) {
        const auto row=value.toObject();
        if(row.value("slot").toInt()==claim->slot&&row.value("pid").toInteger()==claim->pid&&
           row.value("stamp").toInteger()==claim->stamp&&row.value("claimNonce").toString()==claim->nonce)++matches;
    }
    if(matches!=1||processStamp(claim->pid)!=claim->stamp)return refuse(std::move(result),Code::Conflict);
    result.slot=claim->slot;result.claim=claim;return result;
}
Guard acquireOffline(int slot,const Context& context,Options options) {
    if(slot<1||slot>kMaxCounter){Guard result;result.status={Code::Corrupt,{},options.generation};return result;}
    auto result=acquireBase(context,options);if(!result.held())return result;
    Session session;if(loadForRecovery(result.directory,session)!=Loaded::Ok||!validSession(session))return refuse(std::move(result),Code::Corrupt);
    for(const auto& value:session.live) {
        const auto row=value.toObject();const auto stamp=processStamp(row.value("pid").toInteger());
        if(stamp==1||(stamp!=0&&(row.value("stamp").toInteger()==1||stamp==row.value("stamp").toInteger())))return refuse(std::move(result),Code::Busy);
    }
    result.slot=slot;return result; // read-only; no session counter/reset/prune
}
}
