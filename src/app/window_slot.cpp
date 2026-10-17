#include "glancelot/window_slot.hpp"
#include "glancelot/settings_file.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
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
    const QList<QByteArray> fields = line.mid(close + 2).split(' ');
    return fields.size() > 19 ? fields.at(19).toLongLong() + 2 : 1; // starttime (field 22), never 0 or 1
#endif
}

struct Session {
    int counter{};
    QJsonArray live;
};

QString statePath(const QString& directory) { return QDir(directory).filePath("sessions.json"); }

Session load(const QString& directory) {
    Session session;
    QFile file(statePath(directory));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return session;
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("schema").toInt() != 1) return session;
    session.counter = std::max(0, root.value("counter").toInt());
    session.live = root.value("live").toArray();
    return session;
}

bool store(const QString& directory, const Session& session) {
    QSaveFile file(statePath(directory));
    const QJsonObject root{{"schema", 1}, {"counter", session.counter}, {"live", session.live}};
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

// Runs `body(session)` under the cross-process lock; false when the lock or the write failed.
template <typename Body>
bool locked(const QString& directory, Body body) {
    if (!QDir().mkpath(directory)) return false;
    QLockFile lock(statePath(directory) + ".lock");
    lock.setStaleLockTime(15000);
    if (!lock.tryLock(10000)) return false; // a cold-start burst of windows may queue briefly; losing the slot is worse
    Session session = load(directory);
    body(session);
    return store(directory, session);
}
}

WindowSlot::WindowSlot(const QString& directory) : directory_(directory.isEmpty() ? glancelot::settingsDirectory() : directory) {
    const qint64 pid = QCoreApplication::applicationPid();
    const qint64 stamp = processStamp(pid);
    int claimed = 0;
    const bool ok = locked(directory_, [&](Session& session) {
        QJsonArray alive;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            const qint64 other = object.value("pid").toInteger();
            if (processStamp(other) != 0 && processStamp(other) == object.value("stamp").toInteger()) alive.append(object);
        }
        session.live = alive;
        if (session.live.isEmpty()) session.counter = 0; // a new session: every window of the last one is gone
        claimed = ++session.counter;
        session.live.append(QJsonObject{{"slot", claimed}, {"pid", pid}, {"stamp", stamp}});
    });
    number_ = ok ? claimed : 0;
}

void WindowSlot::release() {
    if (number_ == 0) return;
    const qint64 pid = QCoreApplication::applicationPid();
    const int slot = number_;
    number_ = 0;
    locked(directory_, [&](Session& session) {
        QJsonArray remaining;
        for (const auto& entry : std::as_const(session.live)) {
            const auto object = entry.toObject();
            if (!(object.value("pid").toInteger() == pid && object.value("slot").toInt() == slot)) remaining.append(object);
        }
        session.live = remaining;
        if (session.live.isEmpty()) session.counter = 0;
    });
}

WindowSlot::~WindowSlot() { release(); }
