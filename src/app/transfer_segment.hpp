#pragma once
// The memory a decoder worker hands decoded pixels back through. The viewer creates it, the worker attaches by key and writes,
// and the viewer reads only after the worker's "decoded" message, so the pixels are complete and there is nothing to lock.
//
// Windows: a named page-file mapping with no cross-process lock. QSharedMemory is not usable there: it takes a named system
// semaphore (count 1, no owner) in attach, detach and destroy, so a worker killed or crashed inside its own attach or detach
// never gives the count back and the next caller waits forever. The viewer was that caller, on the GUI thread (STACK-CONFIRM
// R3-B2: a permanent window freeze in ordinary slideshow use). With a plain mapping the kernel keeps the memory until the last
// handle closes, so a dead worker's share goes when it dies, and closing the viewer's share never waits for anyone.
//
// Elsewhere: QSharedMemory (System V). Its semaphore is released when the holder dies (SEM_UNDO), so a dead worker cannot block
// anyone, and System V segments avoid the small /dev/shm of a container, where POSIX shared memory could run out.
//
// The class deliberately has no lock: no caller may wait on another process.
#include <QString>
#include <QUuid>
#include <QtGlobal>
#include <string>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QSharedMemory>
#endif

namespace glancelot::detail {
#ifdef Q_OS_WIN
class TransferSegment {
public:
    explicit TransferSegment(QString key) : transferKey(std::move(key)) {}
    ~TransferSegment() { release(); }
    TransferSegment(const TransferSegment&) = delete;
    TransferSegment& operator=(const TransferSegment&) = delete;

    // Fails if the key is already in use: a key belongs to one transfer.
    bool create(qint64 bytes) {
        if (mapping || bytes <= 0) { error = QStringLiteral("Invalid transfer segment request."); return false; }
        const auto size = quint64(bytes);
        HANDLE created = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(size >> 32), DWORD(size & 0xFFFFFFFFu), name().c_str());
        if (!created) { error = qt_error_string(int(GetLastError())); return false; }
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(created); error = QStringLiteral("The transfer key is already in use."); return false; }
        return map(created);
    }
    bool attach() {
        if (mapping) { error = QStringLiteral("Already attached."); return false; }
        HANDLE opened = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name().c_str());
        if (!opened) { error = qt_error_string(int(GetLastError())); return false; }
        return map(opened);
    }
    QString key() const { return transferKey; }
    qint64 size() const { return bytes; }
    void* data() { return view; }
    const void* constData() const { return view; }
    QString errorString() const { return error; }

private:
    std::wstring name() const { return L"Local\\" + transferKey.toStdWString(); }
    bool map(HANDLE handle) {
        void* mapped = MapViewOfFile(handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
        MEMORY_BASIC_INFORMATION region{};
        if (!mapped || VirtualQuery(mapped, &region, sizeof(region)) != sizeof(region)) {
            error = qt_error_string(int(GetLastError()));
            if (mapped) UnmapViewOfFile(mapped);
            CloseHandle(handle);
            return false;
        }
        mapping = handle; view = mapped; bytes = qint64(region.RegionSize);
        return true;
    }
    void release() {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        view = nullptr; mapping = nullptr; bytes = 0;
    }
    QString transferKey, error;
    HANDLE mapping{};
    void* view{};
    qint64 bytes{};
};
#else
class TransferSegment {
public:
    explicit TransferSegment(const QString& key) : memory(key) {}
    bool create(qint64 bytes) { return memory.create(bytes); }
    bool attach() { return memory.attach(QSharedMemory::ReadWrite); }
    QString key() const { return memory.key(); }
    qint64 size() const { return memory.size(); }
    void* data() { return memory.data(); }
    const void* constData() const { return memory.constData(); }
    QString errorString() const { return memory.errorString(); }

private:
    QSharedMemory memory;
};
#endif
inline QString newTransferKey() { return QStringLiteral("Glancelot-") + QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
