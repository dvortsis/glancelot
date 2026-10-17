#pragma once
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QString>
#include <QThread>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

// A cross-process mutex on a file, released by the operating system when the holder exits or crashes, so there is
// never a stale lock to detect. (QLockFile's stale-lock file protocol proved starvation-prone on Windows when
// several processes poll it: a lock file stuck delete-pending kept answering "access denied".)
//  * Windows: the file is opened with no sharing; a second opener gets a sharing violation until it is closed.
//  * POSIX: flock(LOCK_EX | LOCK_NB) on the open file.
// The waiter polls every 5-20 ms with jitter, so a free lock is noticed quickly. The lock file itself is left in
// place; it holds no data.
class FileLock {
public:
    explicit FileLock(const QString& path) : path_(path) {}
    ~FileLock() { release(); }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    // True when held. On false, contended() says whether another process had it (a timeout) as opposed to the
    // lock file being impossible to create or open (read-only or missing folder).
    bool acquire(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            const Attempt result = attempt();
            if (result == Attempt::Held) return true;
            contended_ = result == Attempt::Contended;
            // Transient errors (antivirus or an indexer touching the file) get a short grace period only.
            if (timeoutMs <= 0 || timer.hasExpired(contended_ ? timeoutMs : std::min(timeoutMs, 400))) return false;
            QThread::msleep(quint32(5 + QRandomGenerator::global()->bounded(15)));
        }
    }
    bool contended() const { return contended_; }
    void release() {
#ifdef Q_OS_WIN
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); fd_ = -1; }
#endif
    }
private:
    enum class Attempt { Held, Contended, Failed };
    Attempt attempt() {
#ifdef Q_OS_WIN
        handle_ = CreateFileW(reinterpret_cast<LPCWSTR>(path_.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE) return Attempt::Held;
        const DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ? Attempt::Contended : Attempt::Failed;
#else
        const int fd = ::open(path_.toLocal8Bit().constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) return Attempt::Failed;
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0) { fd_ = fd; return Attempt::Held; }
        const int error = errno;
        ::close(fd);
        return error == EWOULDBLOCK ? Attempt::Contended : Attempt::Failed;
#endif
    }
    QString path_;
    bool contended_{false};
#ifdef Q_OS_WIN
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int fd_{-1};
#endif
};
