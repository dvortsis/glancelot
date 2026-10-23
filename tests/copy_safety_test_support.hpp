#pragma once
// COPY-SAFETY-01 test helpers shared by copy_safety_tests.cpp and copy_move_tests.cpp. Only the public
// CopyMoveService API and the operating system, so the alias and metadata tests also build against the
// pre-fix service for fail-before evidence.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStringList>
#include "glancelot/copy_move_service.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cstest {

using glancelot::CopyMoveService;

// Deterministic, non-repeating bytes so a truncated or mixed-up file never matches by accident.
inline QByteArray makeData(int size, quint32 seed) {
    QByteArray data(size, Qt::Uninitialized);
    quint32 state = seed * 2654435761u + 1u;
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        data[i] = static_cast<char>(state >> 24);
    }
    return data;
}

inline bool writeBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

inline QByteArray sha256Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result().toHex();
}

// Every entry in `dir`, hidden and system ones included, sorted. Data-safety tests compare this with the exact
// expected names, so a leaked temporary or replace backup of any name fails the test.
inline QStringList entriesIn(const QString& dir) {
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    names.sort();
    return names;
}

struct BatchOutcome {
    bool finished{false};
    int succeeded{-1};
    int failed{-1};
    QString lastError;
};

inline BatchOutcome runTransfer(CopyMoveService& service, const QString& source, const QString& destinationDir,
                                CopyMoveService::TransferMode mode,
                                CopyMoveService::CollisionPolicy policy = CopyMoveService::CollisionPolicy::Overwrite) {
    QSignalSpy batchSpy(&service, &CopyMoveService::batchFinished);
    QSignalSpy itemSpy(&service, &CopyMoveService::itemFinished);
    service.enqueueTransfer({source}, destinationDir, mode, policy);
    BatchOutcome outcome;
    outcome.finished = batchSpy.wait(15000);
    if (outcome.finished) {
        outcome.succeeded = batchSpy.at(0).at(1).toInt();
        outcome.failed = batchSpy.at(0).at(2).toInt();
    }
    for (const auto& args : itemSpy) {
        if (!args.at(2).toBool()) outcome.lastError = args.at(3).toString();
    }
    return outcome;
}

inline const char* modeName(CopyMoveService::TransferMode mode) {
    return mode == CopyMoveService::TransferMode::Copy ? "copy" : "move";
}

// A second name for the folder `target`: a junction on Windows (no privilege needed), a symbolic link elsewhere.
inline bool makeFolderAlias(const QString& target, const QString& alias) {
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                                              QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
    return process.waitForFinished(15000) && process.exitCode() == 0 && QFileInfo::exists(alias);
#else
    return ::symlink(QFile::encodeName(target).constData(), QFile::encodeName(alias).constData()) == 0;
#endif
}

inline bool removeFolderAlias(const QString& alias) {
#ifdef Q_OS_WIN
    return RemoveDirectoryW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(alias).utf16())) != 0;
#else
    return ::unlink(QFile::encodeName(alias).constData()) == 0;
#endif
}

inline bool makeHardLink(const QString& existing, const QString& link) {
#ifdef Q_OS_WIN
    return CreateHardLinkW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(link).utf16()),
                           reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(existing).utf16()), nullptr) != 0;
#else
    return ::link(QFile::encodeName(existing).constData(), QFile::encodeName(link).constData()) == 0;
#endif
}

// A file symbolic link; false when the platform or account cannot create one (Windows without Developer Mode).
inline bool makeFileSymlink(const QString& target, const QString& link) {
#ifdef Q_OS_WIN
    return CreateSymbolicLinkW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(link).utf16()),
                               reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(target).utf16()),
                               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
#else
    return ::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()) == 0;
#endif
}

#ifdef Q_OS_WIN
inline std::wstring nativeW(const QString& path) {
    return QDir::toNativeSeparators(path).toStdWString();
}

// A drive letter mapped to `folder` with subst for the scope's lifetime; removed even after a failed QVERIFY.
class SubstDrive {
public:
    explicit SubstDrive(const QString& folder) {
        const DWORD used = GetLogicalDrives();
        for (char letter = 'Z'; letter >= 'G'; --letter) {
            if (used & (1u << (letter - 'A'))) continue;
            const QString drive = QStringLiteral("%1:").arg(QLatin1Char(letter));
            QProcess process;
            process.start(QStringLiteral("subst"), {drive, QDir::toNativeSeparators(folder)});
            if (process.waitForFinished(15000) && process.exitCode() == 0 && QFileInfo::exists(drive + QStringLiteral("/"))) {
                m_drive = drive;
            }
            break;
        }
    }
    ~SubstDrive() {
        if (m_drive.isEmpty()) return;
        QProcess process;
        process.start(QStringLiteral("subst"), {m_drive, QStringLiteral("/d")});
        process.waitForFinished(15000);
    }
    SubstDrive(const SubstDrive&) = delete;
    SubstDrive& operator=(const SubstDrive&) = delete;
    bool isValid() const { return !m_drive.isEmpty(); }
    QString root() const { return m_drive + QStringLiteral("/"); }

private:
    QString m_drive;
};

inline DWORD attributesOf(const QString& path) {
    return GetFileAttributesW(nativeW(path).c_str());
}

inline bool addAttributes(const QString& path, DWORD attributes) {
    const DWORD current = attributesOf(path);
    return current != INVALID_FILE_ATTRIBUTES && SetFileAttributesW(nativeW(path).c_str(), (current & ~FILE_ATTRIBUTE_NORMAL) | attributes);
}

inline bool clearAttributes(const QString& path) {
    return SetFileAttributesW(nativeW(path).c_str(), FILE_ATTRIBUTE_NORMAL) != 0;
}

inline bool writeStream(const QString& path, const char* stream, const QByteArray& data) {
    const std::wstring name = nativeW(path) + L":" + QString::fromLatin1(stream).toStdWString();
    const HANDLE handle = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(handle, data.constData(), DWORD(data.size()), &written, nullptr) && written == DWORD(data.size());
    CloseHandle(handle);
    return ok;
}

inline QByteArray readStream(const QString& path, const char* stream) {
    const std::wstring name = nativeW(path) + L":" + QString::fromLatin1(stream).toStdWString();
    const HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return QByteArray("<missing>");
    QByteArray data(4096, Qt::Uninitialized);
    DWORD read = 0;
    ReadFile(handle, data.data(), DWORD(data.size()), &read, nullptr);
    CloseHandle(handle);
    data.resize(int(read));
    return data;
}

struct AclState {
    bool read{false};
    bool protectedDacl{false};
    int explicitAces{0};
    int inheritedAces{0};
    bool localServiceExplicit{false}; // an explicit entry for NT AUTHORITY\LOCAL SERVICE (S-1-5-19)
};

inline AclState aclStateOf(const QString& path) {
    AclState state;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(nativeW(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &descriptor) != ERROR_SUCCESS) {
        return state;
    }
    state.read = true;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    state.protectedDacl = (control & SE_DACL_PROTECTED) != 0;
    BYTE localService[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(localService);
    CreateWellKnownSid(WinLocalServiceSid, nullptr, localService, &sidSize);
    for (DWORD i = 0; dacl && i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) continue;
        const auto* header = static_cast<ACE_HEADER*>(ace);
        if (header->AceFlags & INHERITED_ACE) {
            ++state.inheritedAces;
            continue;
        }
        ++state.explicitAces;
        if (header->AceType == ACCESS_ALLOWED_ACE_TYPE
            && EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart, localService)) {
            state.localServiceExplicit = true;
        }
    }
    LocalFree(descriptor);
    return state;
}

// Adds an explicit "LOCAL SERVICE: read" entry to the file's DACL, keeping the inherited entries.
inline bool grantLocalServiceRead(const QString& path) {
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(nativeW(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &oldDacl, nullptr, &descriptor) != ERROR_SUCCESS) {
        return false;
    }
    BYTE localService[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(localService);
    CreateWellKnownSid(WinLocalServiceSid, nullptr, localService, &sidSize);
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_READ;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(localService);
    PACL newDacl = nullptr;
    const bool ok = SetEntriesInAclW(1, &access, oldDacl, &newDacl) == ERROR_SUCCESS
        && SetNamedSecurityInfoW(const_cast<LPWSTR>(nativeW(path).c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, newDacl, nullptr) == ERROR_SUCCESS;
    LocalFree(newDacl);
    LocalFree(descriptor);
    return ok;
}

// Replaces the file's DACL with a protected one (no inheritance) that grants only the current user full control.
inline bool restrictToCurrentUser(const QString& path) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    BYTE buffer[512];
    DWORD size = 0;
    const bool gotUser = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size) != 0;
    CloseHandle(token);
    if (!gotUser) return false;
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid);
    PACL dacl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &dacl) != ERROR_SUCCESS) return false;
    const bool ok = SetNamedSecurityInfoW(const_cast<LPWSTR>(nativeW(path).c_str()), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                          nullptr, dacl, nullptr) == ERROR_SUCCESS;
    LocalFree(dacl);
    return ok;
}
#endif

} // namespace cstest
