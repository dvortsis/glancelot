#include "glancelot/window_slot.hpp"
#include "glancelot/settings_file.hpp"
#include "file_lock.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
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
    if (!handle) return 0;
    DWORD code = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    qint64 stamp = 0;
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

WindowSlot::WindowSlot(const QString& directory, int timeoutMs) : directory_(directory.isEmpty() ? glancelot::settingsDirectory() : directory) {
    claim(timeoutMs);
}

bool WindowSlot::claim(int timeoutMs) {
    if (number_ != 0) return true;
    const qint64 pid = QCoreApplication::applicationPid();
    const qint64 stamp = std::max<qint64>(1, processStamp(pid));
    int claimed = 0;
    const bool ok = locked(directory_, timeoutMs, [&](Session& session) {
        QJsonArray alive;
        int highestLiveSlot=0;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            const qint64 stampNow = processStamp(object.value("pid").toInteger());
            const int liveSlot=object.value("slot").toInt();
            if (stampNow != 0 && stampNow == object.value("stamp").toInteger() && liveSlot>0 && liveSlot<=kMaxCounter) {
                alive.append(object);
                highestLiveSlot=std::max(highestLiveSlot,liveSlot);
            }
        }
        session.live = alive;
        if (session.live.isEmpty()) session.counter = 0; // a new session
        else session.counter=std::max(session.counter,highestLiveSlot);
        if (session.counter >= kMaxCounter) return false; // never reuse a live session's slots
        claimed = ++session.counter;
        session.live.append(QJsonObject{{"slot", claimed}, {"pid", pid}, {"stamp", stamp}});
        return true;
    });
    number_ = ok ? claimed : 0;
    return number_ != 0;
}

void WindowSlot::release() {
    if (number_ == 0) return;
    const qint64 pid = QCoreApplication::applicationPid();
    const int slot = number_;
    number_ = 0;
    locked(directory_, 3000, [&](Session& session) {
        QJsonArray remaining;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            if (!(object.value("pid").toInteger() == pid && object.value("slot").toInt() == slot)) remaining.append(object);
        }
        session.live = remaining;
        if (session.live.isEmpty()) session.counter = 0;
        return true;
    });
}

WindowSlot::~WindowSlot() { release(); }
