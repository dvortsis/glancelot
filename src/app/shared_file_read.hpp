#pragma once
// EN-11: read-only opens of original media by in-process readers (metadata,
// animation frames), with the decoder worker's share mode.
#include <QDir>
#include <QFile>
#include <QString>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace glancelot::detail {
// QFile opens without FILE_SHARE_DELETE on Windows, so while the viewer reads an
// image nobody can delete it or rename it away (Explorer, save-via-backup
// editors). Delete sharing allows both. It does not allow a replace onto the held
// name (MoveFileEx REPLACE_EXISTING / QSaveFile commit): Windows refuses that while
// any handle is open, so readers still keep their handles short. If the native
// open fails (for example a path only QFile's long-path handling reaches), fall
// back to QFile so no file that was readable before becomes unreadable.
inline bool openSharedRead(QFile& file, const QString& path) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
        if (descriptor == -1) { CloseHandle(handle); return false; }
        if (file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) return true;
        _close(descriptor);
        return false;
    }
#endif
    file.setFileName(path);
    return file.open(QIODevice::ReadOnly);
}
}
