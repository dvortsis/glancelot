#include "glancelot/copy_move_service.hpp"
#include "asset_group_internal.hpp"
#include "copy_safety.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QRunnable>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QThread>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <map>
#include <utility>
#include <optional>
#include <stop_token>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <io.h>
#include <cstring>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace glancelot {

namespace {

QString clean(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool isSameVolume(const QString& pathA, const QString& pathB) {
#ifdef Q_OS_WIN
    const QString cleanA = clean(pathA);
    const QString cleanB = clean(pathB);
    if (cleanA.length() >= 2 && cleanB.length() >= 2) {
        if (cleanA.at(1) == QLatin1Char(':') && cleanB.at(1) == QLatin1Char(':')) {
            return cleanA.at(0).toUpper() == cleanB.at(0).toUpper();
        }
    }
    // UNC or relative paths: check volume path root
    wchar_t volA[MAX_PATH] = {0};
    wchar_t volB[MAX_PATH] = {0};
    if (GetVolumePathNameW(cleanA.toStdWString().c_str(), volA, MAX_PATH) &&
        GetVolumePathNameW(cleanB.toStdWString().c_str(), volB, MAX_PATH)) {
        return _wcsicmp(volA, volB) == 0;
    }
    return true;
#else
    struct stat stA, stB;
    if (stat(QFile::encodeName(pathA).constData(), &stA) == 0 &&
        stat(QFile::encodeName(pathB).constData(), &stB) == 0) {
        return stA.st_dev == stB.st_dev;
    }
    return true;
#endif
}

QString resolveCollision(const QString& targetPath, CopyMoveService::CollisionPolicy policy) {
    if (!QFileInfo::exists(targetPath) && !QDir(targetPath).exists()) {
        return targetPath;
    }

    if (policy == CopyMoveService::CollisionPolicy::Overwrite) {
        return targetPath;
    }

    if (policy == CopyMoveService::CollisionPolicy::Skip) {
        return QString();
    }

    // RenameBoth: generate name (2).ext, name (3).ext...
    QFileInfo info(targetPath);
    const QString dir = info.dir().absolutePath();
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : (QLatin1Char('.') + info.suffix());

    int counter = 2;
    while (counter < 10000) {
        const QString candidate = QDir(dir).filePath(QStringLiteral("%1 (%2)%3").arg(baseName).arg(counter).arg(suffix));
        if (!QFileInfo::exists(candidate) && !QDir(candidate).exists()) {
            return candidate;
        }
        ++counter;
    }
    return targetPath;
}

// Why an Overwrite of `targetPath` by the file `source` must not start, or an empty string when it may.
QString overwriteRefusal(const QFileInfo& source, const QString& targetPath) {
    const QFileInfo target(targetPath);
    if (!target.exists()) return QString();
    if (target.isDir()) return QStringLiteral("Destination is a folder.");
    if (target.canonicalFilePath() == source.canonicalFilePath()) {
        return QStringLiteral("Source and destination are the same file.");
    }
    // Path text misses aliases (subst letters, junctions, folder-mounted volumes, bind mounts) and hard links.
    switch (copy_safety::compareFileIdentity(source.absoluteFilePath(), targetPath)) {
    case copy_safety::FileIdentityMatch::Same:
        return QStringLiteral("Source and destination are the same file.");
    case copy_safety::FileIdentityMatch::Unknown:
        return QStringLiteral("Could not confirm that source and destination are different files.");
    case copy_safety::FileIdentityMatch::Different:
        break;
    }
    // The owner-write bit (Windows: the read-only attribute) marks a file the user protected; an administrator
    // or root account could otherwise replace it.
    if (!target.isWritable() || !target.permission(QFileDevice::WriteOwner)) {
        return QStringLiteral("Destination file is read-only.");
    }
    return QString();
}

void collectStats(const QString& path, int& itemCount, qint64& byteCount) {
    QFileInfo info(path);
    if (!info.exists()) return;

    if (info.isDir() && !info.isSymbolicLink()) {
        ++itemCount;
        QDir dir(path);
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& entry : entries) {
            collectStats(entry.absoluteFilePath(), itemCount, byteCount);
        }
    } else {
        ++itemCount;
        byteCount += info.size();
    }
}

} // namespace

namespace copy_safety {

namespace {

QMutex& safetyMutex() {
    static QMutex mutex;
    return mutex;
}

// Temporaries a copy in this process is writing right now; removeStaleTemporaries() leaves them alone.
// Keyed by inFlightKey(), so two batches that spell the same folder differently still see each other.
QSet<QString>& inFlightTemporaries() {
    static QSet<QString> paths;
    return paths;
}

QString inFlightKey(const QString& path) {
    const QString cleaned = clean(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return cleaned.toCaseFolded(); // Windows file names compare case-insensitively
#else
    return cleaned;
#endif
}

#ifdef Q_OS_WIN
// Absolute path with the \\?\ prefix (\\?\UNC\ for shares), so Win32 calls accept paths over MAX_PATH.
std::wstring winPath(const QString& path) {
    QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QLatin1String("\\\\?\\")) || native.startsWith(QLatin1String("\\\\.\\"))) {
        return native.toStdWString();
    }
    native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (native.startsWith(QLatin1String("\\\\"))) {
        return (QLatin1String("\\\\?\\UNC\\") + native.mid(2)).toStdWString();
    }
    return (QLatin1String("\\\\?\\") + native).toStdWString();
}

QString windowsError(DWORD code) {
    return QStringLiteral("Windows error %1").arg(code);
}

bool pathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Whether the file at `path` carries permissions of its own: a protected DACL or any explicit (not
// inherited) entry. False on a volume without persistent ACLs (FAT, exFAT). Returns false on error.
bool readExplicitProtection(const std::wstring& path, bool& hasExplicit, QString* error) {
    hasExplicit = false;
    const HANDLE handle = CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = windowsError(GetLastError());
        return false;
    }
    DWORD fsFlags = 0;
    if (!GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &fsFlags, nullptr, 0)) {
        fsFlags = FILE_PERSISTENT_ACLS; // unknown volume: assume it has permissions to keep
    }
    bool ok = true;
    if (fsFlags & FILE_PERSISTENT_ACLS) {
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD rc = GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                                         nullptr, &descriptor);
        if (rc != ERROR_SUCCESS) {
            if (error) *error = windowsError(rc);
            ok = false;
        } else {
            SECURITY_DESCRIPTOR_CONTROL control = 0;
            DWORD revision = 0;
            if (!GetSecurityDescriptorControl(descriptor, &control, &revision) || (control & SE_DACL_PROTECTED)) {
                hasExplicit = true;
            }
            for (DWORD i = 0; dacl && i < dacl->AceCount && !hasExplicit; ++i) {
                void* ace = nullptr;
                if (!GetAce(dacl, i, &ace) || !(static_cast<ACE_HEADER*>(ace)->AceFlags & INHERITED_ACE)) {
                    hasExplicit = true;
                }
            }
            LocalFree(descriptor);
        }
    }
    CloseHandle(handle);
    return ok;
}

// MoveFileExW replace for volumes where ReplaceFileW does not work. Carries the target's hidden, system and
// not-indexed attributes over, and refuses (target untouched) when the target has permissions of its own,
// because the replacement would silently take the folder's inherited permissions instead.
bool replaceWithoutMerge(const std::wstring& from, const std::wstring& to, QString* error) {
    bool hasExplicit = false;
    if (!readExplicitProtection(to, hasExplicit, error)) return false;
    if (hasExplicit) {
        if (error) *error = QStringLiteral("the destination has its own permissions, which this volume cannot carry over");
        return false;
    }
    constexpr DWORD kept = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    const DWORD targetAttributes = GetFileAttributesW(to.c_str());
    const DWORD replacementAttributes = GetFileAttributesW(from.c_str());
    if (targetAttributes == INVALID_FILE_ATTRIBUTES || replacementAttributes == INVALID_FILE_ATTRIBUTES) {
        if (error) *error = windowsError(GetLastError());
        return false;
    }
    DWORD wanted = (replacementAttributes & ~kept) | (targetAttributes & kept);
    if (wanted == 0) wanted = FILE_ATTRIBUTE_NORMAL;
    const bool changed = wanted != replacementAttributes;
    if (changed && !SetFileAttributesW(from.c_str(), wanted)) {
        if (error) *error = windowsError(GetLastError());
        return false;
    }
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        if (changed) SetFileAttributesW(from.c_str(), replacementAttributes);
        if (error) *error = windowsError(code);
        return false;
    }
    return true;
}

// Name for ReplaceFileW's backup of the replaced target, beside it. Not swept as a leftover.
QString backupPathFor(const QString& targetPath) {
    const QString suffix = QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, QLatin1Char('0'));
    return clean(QFileInfo(targetPath).dir().filePath(QStringLiteral(".~gb%1.tmp").arg(suffix)));
}
#endif

FaultHook& faultHook() {
    static FaultHook hook;
    return hook;
}

bool injectFault(FaultPoint point, const QString& path, qint64 bytes) {
    FaultHook hook;
    {
        QMutexLocker locker(&safetyMutex());
        hook = faultHook();
    }
    return hook && hook(point, path, bytes);
}

// Flushes Qt's buffer and the operating system's cache for an open file to the device.
bool syncToDisk(QFile& file) {
    if (!file.flush()) return false;
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    return handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle);
#else
    return ::fsync(file.handle()) == 0;
#endif
}

// A temporary file being written for one Overwrite item. Removes the file on destruction unless released.
class TemporaryCopy {
public:
    explicit TemporaryCopy(const QString& targetPath) {
        for (int attempt = 0; attempt < 8 && path_.isEmpty(); ++attempt) {
            const QString candidate = temporaryPathFor(targetPath);
            file_.setFileName(candidate);
            // NewOnly: never open (and so never truncate) an existing file, including another copy's temporary.
            if (file_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                path_ = candidate;
                QMutexLocker locker(&safetyMutex());
                inFlightTemporaries().insert(inFlightKey(path_));
            } else if (QFileInfo::exists(candidate)) {
                continue;
            } else {
                break;
            }
        }
    }
    ~TemporaryCopy() {
        if (path_.isEmpty()) return;
        file_.close();
        if (!released_) QFile::remove(path_);
        QMutexLocker locker(&safetyMutex());
        inFlightTemporaries().remove(inFlightKey(path_));
    }
    TemporaryCopy(const TemporaryCopy&) = delete;
    TemporaryCopy& operator=(const TemporaryCopy&) = delete;

    bool isOpen() const { return !path_.isEmpty(); }
    QFile& file() { return file_; }
    const QString& path() const { return path_; }
    void release() { released_ = true; }

private:
    QFile file_;
    QString path_;
    bool released_{false};
};

} // namespace

QString temporaryPathFor(const QString& targetPath) {
    const QString suffix = QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, QLatin1Char('0'));
    return clean(QFileInfo(targetPath).dir().filePath(QStringLiteral(".~gl%1.tmp").arg(suffix)));
}

bool isTemporaryName(const QString& fileName) {
    static const QRegularExpression pattern(QStringLiteral("^\\.~gl[0-9a-f]{8}\\.tmp$"));
    return pattern.match(fileName).hasMatch();
}

FileIdentityMatch compareFileIdentity(const QString& pathA, const QString& pathB) {
#ifdef Q_OS_WIN
    struct Identity {
        bool open{false};
        bool hasFileIdInfo{false};
        FILE_ID_INFO idInfo{};
        bool hasHandleInfo{false};
        BY_HANDLE_FILE_INFORMATION handleInfo{};
    };
    const auto identify = [](const QString& path) {
        Identity id;
        // No data access, so the share mode of other openers does not matter; follows links and junctions.
        const HANDLE handle = CreateFileW(winPath(path).c_str(), FILE_READ_ATTRIBUTES,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return id;
        id.open = true;
        if (GetFileInformationByHandleEx(handle, FileIdInfo, &id.idInfo, sizeof(id.idInfo))) {
            static const FILE_ID_128 zero{};
            id.hasFileIdInfo = std::memcmp(&id.idInfo.FileId, &zero, sizeof(zero)) != 0;
        }
        id.hasHandleInfo = GetFileInformationByHandle(handle, &id.handleInfo) != 0;
        CloseHandle(handle);
        return id;
    };
    const Identity a = identify(pathA);
    const Identity b = identify(pathB);
    if (!a.open || !b.open) return FileIdentityMatch::Unknown;
    if (a.hasFileIdInfo && b.hasFileIdInfo) {
        if (a.idInfo.VolumeSerialNumber != b.idInfo.VolumeSerialNumber) return FileIdentityMatch::Different;
        return std::memcmp(&a.idInfo.FileId, &b.idInfo.FileId, sizeof(FILE_ID_128)) == 0 ? FileIdentityMatch::Same
                                                                                          : FileIdentityMatch::Different;
    }
    if (a.hasHandleInfo && b.hasHandleInfo) {
        if (a.handleInfo.dwVolumeSerialNumber != b.handleInfo.dwVolumeSerialNumber) return FileIdentityMatch::Different;
        const quint64 indexA = (quint64(a.handleInfo.nFileIndexHigh) << 32) | a.handleInfo.nFileIndexLow;
        const quint64 indexB = (quint64(b.handleInfo.nFileIndexHigh) << 32) | b.handleInfo.nFileIndexLow;
        if (indexA == 0 || indexB == 0) return FileIdentityMatch::Unknown; // some redirectors report no index
        return indexA == indexB ? FileIdentityMatch::Same : FileIdentityMatch::Different;
    }
    return FileIdentityMatch::Unknown;
#else
    struct stat stA, stB;
    if (::stat(QFile::encodeName(pathA).constData(), &stA) != 0 || ::stat(QFile::encodeName(pathB).constData(), &stB) != 0) {
        return FileIdentityMatch::Unknown;
    }
    if (stA.st_dev != stB.st_dev) return FileIdentityMatch::Different;
    return stA.st_ino == stB.st_ino ? FileIdentityMatch::Same : FileIdentityMatch::Different;
#endif
}

bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error,
                           bool* originalDisplaced) {
    if (originalDisplaced) *originalDisplaced = false;
    if (injectFault(FaultPoint::Replace, targetPath, 0)) {
        if (error) *error = QStringLiteral("injected replace failure");
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring from = winPath(replacementPath);
    const std::wstring to = winPath(targetPath);
    if (!pathExists(to)) {
        // Nothing to replace (the target vanished meanwhile): a plain rename, which never overwrites anything.
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) {
            if (error) *error = windowsError(GetLastError());
            return false;
        }
        return true;
    }

    // ReplaceFileW keeps the replaced file's attributes, DACL and streams. With a backup name every partial
    // failure leaves the original under a known name: 1175 and 1176 leave both files where they were, and
    // 1177 leaves the original as the backup, which is moved back below.
    QString backupPath = backupPathFor(targetPath);
    for (int attempt = 0; attempt < 8 && QFileInfo::exists(backupPath); ++attempt) backupPath = backupPathFor(targetPath);
    // An existing ".~gb" may be the only copy of an earlier original (COPY-SAFETY-01 review N2): never reuse one.
    if (QFileInfo::exists(backupPath)) {
        if (error) *error = QStringLiteral("no free backup name beside the target");
        return false;
    }
    const std::wstring backup = winPath(backupPath);
    DWORD code = ERROR_SUCCESS;
    if (injectFault(FaultPoint::ReplaceUnsupported, targetPath, 0)) {
        code = ERROR_NOT_SUPPORTED;
    } else if (injectFault(FaultPoint::ReplacePartial, targetPath, 0)) {
        code = MoveFileExW(to.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH) ? ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
                                                                               : GetLastError();
    } else if (!ReplaceFileW(to.c_str(), from.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
        code = GetLastError();
    }
    if (code == ERROR_SUCCESS) {
        // The backup is the old target; the user asked to replace it. If it cannot be deleted now (for
        // example another program holds it), it stays as a hidden-by-name ".~gb*.tmp" file.
        DeleteFileW(backup.c_str());
        return true;
    }

    switch (code) {
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_PARAMETER:
    case ERROR_CALL_NOT_IMPLEMENTED:
        if (!pathExists(backup) && pathExists(to)) return replaceWithoutMerge(from, to, error);
        break;
    default:
        break;
    }

    if (pathExists(backup)) {
        if (!pathExists(to)) {
            // The original was renamed to the backup name and the replacement did not take its place: put it back.
            const bool restored = !injectFault(FaultPoint::RestoreBackup, targetPath, 0)
                && MoveFileExW(backup.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH);
            if (!restored) {
                if (originalDisplaced) *originalDisplaced = true;
                if (error) {
                    *error = QStringLiteral("%1; the original file is kept as %2")
                                 .arg(windowsError(code), QDir::toNativeSeparators(backupPath));
                }
                return false;
            }
        } else if (error) {
            *error = QStringLiteral("%1; a copy of the original also remains as %2")
                         .arg(windowsError(code), QDir::toNativeSeparators(backupPath));
            return false;
        }
    }
    if (error) *error = windowsError(code);
    return false;
#else
    // The replacement takes the target's permission bits, so a target the user restricted stays restricted.
    // Only rwx bits: never carry a set-id or sticky bit onto new content.
    const QByteArray from = QFile::encodeName(replacementPath);
    const QByteArray to = QFile::encodeName(targetPath);
    struct stat targetStat {};
    struct stat replacementStat {};
    bool modeChanged = false;
    if (::stat(to.constData(), &targetStat) == 0 && ::stat(from.constData(), &replacementStat) == 0) {
        const mode_t wanted = targetStat.st_mode & 0777;
        if ((replacementStat.st_mode & 07777) != wanted) {
            if (::chmod(from.constData(), wanted) != 0) {
                if (error) *error = QStringLiteral("could not keep the destination's permissions: %1")
                                        .arg(QString::fromLocal8Bit(std::strerror(errno)));
                return false;
            }
            modeChanged = true;
        }
    }
    if (::rename(from.constData(), to.constData()) != 0) {
        const int code = errno;
        if (modeChanged) ::chmod(from.constData(), replacementStat.st_mode & 07777);
        if (error) *error = QString::fromLocal8Bit(std::strerror(code));
        return false;
    }
    // Make the rename itself durable; the data was synced before the rename.
    const int dirFd = ::open(QFile::encodeName(QFileInfo(targetPath).absolutePath()).constData(), O_RDONLY | O_DIRECTORY);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }
    return true;
#endif
}

int removeStaleTemporaries(const QString& directory) {
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    int removed = 0;
    for (const QFileInfo& entry : entries) {
        if (!isTemporaryName(entry.fileName()) || entry.isSymbolicLink()) continue;
        const QString path = clean(entry.absoluteFilePath());
        {
            QMutexLocker locker(&safetyMutex());
            if (inFlightTemporaries().contains(inFlightKey(path))) continue;
        }
        if (QFile::remove(path)) ++removed;
    }
    return removed;
}

void setFaultHookForTesting(FaultHook hook) {
    QMutexLocker locker(&safetyMutex());
    faultHook() = std::move(hook);
}

} // namespace copy_safety

struct TransferTask {
    quint64 batchId{0};
    QStringList sourcePaths;
    QString destDirectory;
    CopyMoveService::TransferMode mode;
    CopyMoveService::CollisionPolicy collisionPolicy;
};

namespace {
// One top-level source of a batch. A planned item carries the name the confirmation showed and its snapshot, checked right
// before the item starts, so what runs is what was confirmed. enqueueTransfer's items carry neither.
struct BatchItem {
    QString source;
    QString targetName;
    std::optional<MutationTarget> snapshot;
};
struct PlannedItem {
    BatchItem item;
    bool skippedBySkip{}; // it collides, or under asset scope another member of its asset does
};
struct TransferPlan {
    CopyMoveService::TransferMode mode{CopyMoveService::TransferMode::Copy};
    std::vector<PlannedItem> items;
    QVariantMap summary;
    bool cancelled{};
};
QString transferKey(const QString& path) {
#ifdef Q_OS_WIN
    return clean(path).toCaseFolded();
#else
    return clean(path);
#endif
}
QVariantMap rejection(const std::filesystem::path& path, const QString& reason) {
    const QString shown = assets::detail::displayPath(path);
    return {{"path", shown}, {"name", QFileInfo(shown).fileName()}, {"reason", reason}};
}

// Worker: the immutable snapshot a transfer confirmation shows. Reads only. One asset keeps one name: when any of its
// members collide, every member takes the same " (n)" under keep-both and none is copied under skip, because a sidecar that
// lands beside a different photo of the same name would describe the wrong picture.
TransferPlan buildTransferPlan(const std::vector<std::filesystem::path>& requested, const QString& destination,
                               CopyMoveService::TransferMode mode, assets::Scope scope, std::stop_token cancel) {
    namespace fs = std::filesystem;
    TransferPlan plan;
    plan.mode = mode;
    const auto expansion = assets::expand(requested, scope,
        mode == CopyMoveService::TransferMode::Move ? assets::Operation::Move : assets::Operation::Copy, nullptr, cancel);
    if (expansion.cancelled) { plan.cancelled = true; return plan; }
    QVariantMap summary;
    assets::detail::addCompanionSummary(summary, expansion, scope);
    const QString destinationDir = clean(destination);
    QVariantList rejected, targets, collisions;
    if (!QFileInfo(destinationDir).isDir()) summary.insert(QStringLiteral("error"), QStringLiteral("destinationMissing"));

    struct Candidate { assets::Planned planned; MutationTarget snapshot; QString name; };
    std::vector<std::vector<Candidate>> assetsInOrder;
    std::map<QString, std::size_t> assetOfKey;
    int files = 0, folders = 0;
    double bytes = 0;
    for (const auto& planned : expansion.included) {
        if (cancel.stop_requested()) { plan.cancelled = true; return plan; }
        std::error_code error;
        const auto snapshot = snapshotTarget(planned.member.path, error);
        if (error) { rejected.append(rejection(planned.member.path, QStringLiteral("inaccessible"))); continue; }
        if (!snapshot) { rejected.append(rejection(planned.member.path, QStringLiteral("missing"))); continue; }
        const QString name = assets::detail::displayPath(planned.member.path.filename());
        const QString parent = assets::detail::displayPath(planned.member.path.parent_path());
        if (mode == CopyMoveService::TransferMode::Move && transferKey(parent) == transferKey(destinationDir)) {
            rejected.append(rejection(planned.member.path, QStringLiteral("sameFolder")));
            continue;
        }
        (snapshot->kind == TargetKind::Folder ? folders : files) += 1;
        bytes += double(snapshot->bytes);
        QString assetKey = transferKey(parent + QLatin1Char('/') + name);
        if (scope == assets::Scope::Asset && snapshot->kind == TargetKind::File) {
            assetKey = transferKey(parent + QLatin1Char('/') + assets::detail::splitAssetName(name, planned.member.sidecar).stem) + QLatin1String("/asset");
        }
        const auto [slot, added] = assetOfKey.try_emplace(assetKey, assetsInOrder.size());
        if (added) assetsInOrder.emplace_back();
        assetsInOrder[slot->second].push_back({planned, *snapshot, name});
    }

    QSet<QString> taken; // names this plan already gave out, so two sources can't aim at one target
    const QDir target(destinationDir);
    const auto occupied = [&](const QString& name) {
        const QString path = clean(target.filePath(name));
        const QFileInfo info(path);
        return info.exists() || info.isSymLink() || taken.contains(transferKey(path));
    };
    for (const auto& asset : assetsInOrder) {
        bool collides = false;
        for (const auto& member : asset) collides = occupied(member.name) || collides;
        const auto splitOf = [&](const Candidate& member) {
            if (asset.size() > 1) return assets::detail::splitAssetName(member.name, member.planned.member.sidecar);
            const QFileInfo info(member.name); // a single item keeps enqueueTransfer's naming
            return assets::detail::AssetName{info.completeBaseName(), info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix()};
        };
        const auto renamed = [&](const Candidate& member, int n) {
            const auto parts = splitOf(member);
            return QStringLiteral("%1 (%2)%3").arg(parts.stem).arg(n).arg(parts.suffix);
        };
        int shared = 0;
        if (collides) {
            for (int n = 2; n < 10000 && !shared; ++n) {
                const bool free = std::none_of(asset.begin(), asset.end(), [&](const Candidate& member) { return occupied(renamed(member, n)); });
                if (free) shared = n;
            }
        }
        for (const auto& member : asset) {
            const QString chosen = shared ? renamed(member, shared) : member.name;
            const QString source = assets::detail::displayPath(member.planned.member.path);
            if (occupied(member.name)) {
                collisions.append(QVariantMap{{"path", source}, {"target", clean(target.filePath(member.name))},
                                              {"requestedBy", assets::detail::displayPath(member.planned.requestedBy)}, {"name", member.name}});
            }
            targets.append(QVariantMap{{"path", source}, {"name", member.name}, {"kind", member.snapshot.kind == TargetKind::Folder ? "folder" : "file"},
                                       {"link", member.snapshot.link}, {"bytes", double(member.snapshot.bytes)}, {"renamedTo", chosen == member.name ? QString() : chosen}});
            plan.items.push_back({{source, chosen, member.snapshot}, collides});
        }
        for (const auto& member : asset) taken.insert(transferKey(target.filePath(shared ? renamed(member, shared) : member.name)));
    }
    summary.insert(QStringLiteral("mode"), mode == CopyMoveService::TransferMode::Move ? QStringLiteral("move") : QStringLiteral("copy"));
    summary.insert(QStringLiteral("destination"), destinationDir);
    summary.insert(QStringLiteral("targets"), targets);
    summary.insert(QStringLiteral("rejected"), rejected);
    summary.insert(QStringLiteral("collisions"), collisions);
    summary.insert(QStringLiteral("fileCount"), files);
    summary.insert(QStringLiteral("folderCount"), folders);
    summary.insert(QStringLiteral("totalBytes"), bytes);
    plan.summary = std::move(summary);
    return plan;
}
}

struct CopyMoveService::Private {
    CopyMoveService* q;
    std::atomic<bool> active{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> cancelled{false};

    std::atomic<int> totalItems{0};
    std::atomic<int> completedItems{0};
    std::atomic<qint64> totalBytes{0};
    std::atomic<qint64> transferredBytes{0};

    mutable QMutex stateMutex;
    QString currentItem;
    QString statusMessage;

    // Clipboard state
    QString clipboardMode; // "cut" | "copy"
    QStringList clipboardTargets;

    quint64 nextBatchId{1};

    // Transfer plans. One newest plan: a later request supersedes it, and it can be confirmed once.
    QThreadPool* planPool = new QThreadPool; // heap-allocated so a worker wedged on a dead share can be abandoned at teardown
    std::stop_source planStop;
    quint64 nextPlanId{}, planningId{}, readyId{};
    std::optional<TransferPlan> ready;

    explicit Private(CopyMoveService* parent) : q(parent) { planPool->setMaxThreadCount(1); }
    ~Private() {
        planStop.request_stop();
        if (planPool->waitForDone(5000)) delete planPool;
    }

    quint64 startBatch(const QList<BatchItem>& items, const QString& destinationDirectory, TransferMode mode, CollisionPolicy collision);

    void resetProgress(int items, qint64 bytes) {
        totalItems = items;
        completedItems = 0;
        totalBytes = bytes;
        transferredBytes = 0;
        {
            QMutexLocker locker(&stateMutex);
            currentItem.clear();
            statusMessage = QStringLiteral("Preparing transfer...");
        }
    }

    void updateStatus(const QString& item, const QString& status) {
        {
            QMutexLocker locker(&stateMutex);
            currentItem = item;
            statusMessage = status;
        }
        QMetaObject::invokeMethod(q, [this]() {
            emit q->progressChanged();
        }, Qt::QueuedConnection);
    }
};

CopyMoveService::CopyMoveService(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}

CopyMoveService::~CopyMoveService() {
    cancel();
}

bool CopyMoveService::active() const {
    return d->active.load();
}

bool CopyMoveService::paused() const {
    return d->paused.load();
}

int CopyMoveService::totalItems() const {
    return d->totalItems.load();
}

int CopyMoveService::completedItems() const {
    return d->completedItems.load();
}

qint64 CopyMoveService::totalBytes() const {
    return d->totalBytes.load();
}

qint64 CopyMoveService::transferredBytes() const {
    return d->transferredBytes.load();
}

qreal CopyMoveService::progress() const {
    const qint64 total = d->totalBytes.load();
    if (total <= 0) {
        const int items = d->totalItems.load();
        if (items <= 0) return 0.0;
        return qBound(0.0, static_cast<qreal>(d->completedItems.load()) / items, 1.0);
    }
    return qBound(0.0, static_cast<qreal>(d->transferredBytes.load()) / total, 1.0);
}

QString CopyMoveService::currentItem() const {
    QMutexLocker locker(&d->stateMutex);
    return d->currentItem;
}

QString CopyMoveService::statusMessage() const {
    QMutexLocker locker(&d->stateMutex);
    return d->statusMessage;
}

bool CopyMoveService::hasClipboardTargets() const {
    QMutexLocker locker(&d->stateMutex);
    return !d->clipboardTargets.isEmpty();
}

QString CopyMoveService::clipboardMode() const {
    QMutexLocker locker(&d->stateMutex);
    return d->clipboardMode;
}

QStringList CopyMoveService::clipboardTargets() const {
    QMutexLocker locker(&d->stateMutex);
    return d->clipboardTargets;
}

void CopyMoveService::pause() {
    if (!d->active.load()) return;
    d->paused.store(true);
    emit pausedChanged();
}

void CopyMoveService::resume() {
    if (!d->active.load()) return;
    d->paused.store(false);
    emit pausedChanged();
}

void CopyMoveService::cancel() {
    d->cancelled.store(true);
    d->paused.store(false);
    emit pausedChanged();
}

void CopyMoveService::markCut(const QStringList& paths) {
    if (paths.isEmpty()) return;
    {
        QMutexLocker locker(&d->stateMutex);
        d->clipboardMode = QStringLiteral("cut");
        d->clipboardTargets = paths;
    }
    emit clipboardChanged();
}

void CopyMoveService::markCopy(const QStringList& paths) {
    if (paths.isEmpty()) return;
    {
        QMutexLocker locker(&d->stateMutex);
        d->clipboardMode = QStringLiteral("copy");
        d->clipboardTargets = paths;
    }
    emit clipboardChanged();
}

void CopyMoveService::pasteTo(const QString& destinationDirectory) {
    QStringList targets;
    QString mode;
    {
        QMutexLocker locker(&d->stateMutex);
        targets = d->clipboardTargets;
        mode = d->clipboardMode;
    }

    if (targets.isEmpty() || destinationDirectory.isEmpty()) return;

    const TransferMode tMode = (mode == QStringLiteral("cut")) ? TransferMode::Move : TransferMode::Copy;
    enqueueTransfer(targets, destinationDirectory, tMode, CollisionPolicy::RenameBoth);

    if (mode == QStringLiteral("cut")) {
        clearClipboard();
    }
}

void CopyMoveService::clearClipboard() {
    {
        QMutexLocker locker(&d->stateMutex);
        d->clipboardMode.clear();
        d->clipboardTargets.clear();
    }
    emit clipboardChanged();
}

quint64 CopyMoveService::enqueueTransfer(const QStringList& sourcePaths,
                                       const QString& destinationDirectory,
                                       TransferMode mode,
                                       CollisionPolicy collision) {
    if (sourcePaths.isEmpty() || destinationDirectory.isEmpty()) return 0;
    QList<BatchItem> items;
    items.reserve(sourcePaths.size());
    for (const auto& path : sourcePaths) items.append({path, {}, std::nullopt});
    return d->startBatch(items, destinationDirectory, mode, collision);
}

quint64 CopyMoveService::planTransfer(const QStringList& sourcePaths, const QString& destinationDirectory, TransferMode mode, const QString& scope) {
    // A newer request supersedes the plan being built and the one waiting for confirmation.
    d->planStop.request_stop();
    d->planStop = std::stop_source{};
    d->ready.reset();
    d->readyId = d->planningId = 0;
    if (sourcePaths.isEmpty() || destinationDirectory.isEmpty()) return 0; // an empty request only withdraws
    const quint64 id = ++d->nextPlanId;
    d->planningId = id;
    std::vector<std::filesystem::path> requested;
    requested.reserve(std::size_t(sourcePaths.size()));
    for (const auto& path : sourcePaths) requested.push_back(assets::detail::nativePath(path));
    auto* watcher = new QFutureWatcher<TransferPlan>(this);
    connect(watcher, &QFutureWatcher<TransferPlan>::finished, this, [this, watcher, id] {
        TransferPlan plan = watcher->result();
        watcher->deleteLater();
        if (id != d->planningId) return; // superseded: nothing stale reaches the confirmation
        d->planningId = 0;
        if (plan.cancelled) return;
        QVariantMap summary = plan.summary;
        summary.insert(QStringLiteral("id"), id);
        d->ready = std::move(plan);
        d->readyId = id;
        emit transferPlanReady(id, summary);
    });
    watcher->setFuture(QtConcurrent::run(d->planPool, [requested = std::move(requested), destinationDirectory, mode,
                                                       scope = assets::detail::scopeFromName(scope), cancel = d->planStop.get_token()] {
        return buildTransferPlan(requested, destinationDirectory, mode, scope, cancel);
    }));
    return id;
}

bool CopyMoveService::confirmTransfer(quint64 id, CollisionPolicy collision, bool permanentReplaceConsent) {
    Q_UNUSED(permanentReplaceConsent) // takes effect with B2's Overwrite
    if (collision == CollisionPolicy::Overwrite) return false; // needs B2's journal and trash probe; the plan stays confirmable
    if (id == 0 || id != d->readyId || !d->ready || d->ready->items.empty() || d->active.load()) return false;
    if (d->ready->summary.contains(QStringLiteral("error"))) return false;
    const QString destination = d->ready->summary.value(QStringLiteral("destination")).toString();
    const TransferMode mode = d->ready->mode;
    QList<BatchItem> items;
    for (const auto& planned : d->ready->items) {
        if (collision == CollisionPolicy::Skip && planned.skippedBySkip) continue;
        items.append(planned.item);
    }
    d->ready.reset();
    d->readyId = 0; // one confirmation per plan
    d->startBatch(items, destination, mode, collision);
    return true;
}

quint64 CopyMoveService::Private::startBatch(const QList<BatchItem>& items, const QString& destinationDirectory, TransferMode mode, CollisionPolicy collision) {
    const quint64 batchId = nextBatchId++;
    active.store(true);
    paused.store(false);
    cancelled.store(false);
    emit q->activeChanged();
    emit q->pausedChanged();

    QPointer<CopyMoveService> self(q);

    QThreadPool::globalInstance()->start([self, batchId, items, destinationDirectory, mode, collision]() {
        if (!self) return;

        // 1. Collect statistics (items and total bytes)
        int totalItemsCount = 0;
        qint64 totalBytesCount = 0;
        for (const auto& item : items) {
            collectStats(item.source, totalItemsCount, totalBytesCount);
        }

        self->d->resetProgress(totalItemsCount, totalBytesCount);
        QMetaObject::invokeMethod(self, [self]() {
            if (self) emit self->progressChanged();
        }, Qt::QueuedConnection);

        int successfulCount = 0;
        int failedCount = 0;
        QStringList mutatedSources;
        QStringList mutatedDestinations;
        QSet<QString> sweptDirectories; // target folders already cleared of crash leftovers (COPY-SAFETY-01)
        QString plannedName; // the confirmed name of the top-level item about to start

        // Helper: recursive copy/move
        std::function<bool(const QString&, const QString&)> processItem;
        processItem = [&](const QString& src, const QString& dstDir) -> bool {
            if (!self || self->d->cancelled.load()) return false;

            while (self && self->d->paused.load() && !self->d->cancelled.load()) {
                QThread::msleep(50);
            }
            if (!self || self->d->cancelled.load()) return false;

            QFileInfo srcInfo(src);
            if (!srcInfo.exists()) {
                ++failedCount;
                return false;
            }

            const QString targetName = plannedName.isEmpty() ? srcInfo.fileName() : std::exchange(plannedName, QString());
            QString destPath = clean(QDir(dstDir).filePath(targetName));

            // Collision check
            destPath = resolveCollision(destPath, collision);
            if (destPath.isEmpty()) {
                // Policy was Skip
                self->d->completedItems.fetch_add(1);
                QMetaObject::invokeMethod(self, [self]() { if (self) emit self->progressChanged(); }, Qt::QueuedConnection);
                return true;
            }

            self->d->updateStatus(targetName, QStringLiteral("%1 %2...").arg(mode == TransferMode::Move ? QStringLiteral("Moving") : QStringLiteral("Copying"), targetName));
            QMetaObject::invokeMethod(self, [self, src, destPath]() {
                if (self) emit self->itemStarted(src, destPath);
            }, Qt::QueuedConnection);

            // 1. Directory processing
            if (srcInfo.isDir() && !srcInfo.isSymbolicLink()) {
                // An existing destination folder that is the source folder through an alias (subst letter,
                // junction, mount): merging it into itself would copy files onto themselves and remove the
                // source's empty subfolders (COPY-SAFETY-01).
                if (QFileInfo::exists(destPath)) {
                    const auto match = copy_safety::compareFileIdentity(src, destPath);
                    if (match != copy_safety::FileIdentityMatch::Different) {
                        const QString refusal = match == copy_safety::FileIdentityMatch::Same
                            ? QStringLiteral("Source and destination are the same folder.")
                            : QStringLiteral("Could not confirm that source and destination are different folders.");
                        ++failedCount;
                        QMetaObject::invokeMethod(self, [self, src, destPath, refusal]() {
                            if (self) emit self->itemFinished(src, destPath, false, refusal);
                        }, Qt::QueuedConnection);
                        return false;
                    }
                }
                const bool sameVol = isSameVolume(src, dstDir);
                if (mode == TransferMode::Move && sameVol && !QFileInfo::exists(destPath)) {
                    // Fast atomic directory rename on same volume
                    if (QDir().rename(src, destPath)) {
                        self->d->completedItems.fetch_add(1);
                        mutatedSources.append(src);
                        mutatedDestinations.append(destPath);
                        ++successfulCount;
                        QMetaObject::invokeMethod(self, [self, src, destPath]() {
                            if (self) emit self->itemFinished(src, destPath, true, QString());
                        }, Qt::QueuedConnection);
                        return true;
                    }
                }

                // Create destination directory
                if (!QDir(destPath).exists() && !QDir().mkpath(destPath)) {
                    ++failedCount;
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Could not create destination folder."));
                    }, Qt::QueuedConnection);
                    return false;
                }

                QDir srcDir(src);
                const QFileInfoList entries = srcDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
                bool allChildrenOk = true;
                for (const auto& entry : entries) {
                    if (!processItem(entry.absoluteFilePath(), destPath)) {
                        allChildrenOk = false;
                    }
                }

                if (mode == TransferMode::Move && allChildrenOk) {
                    // Remove source directory after verified move
                    srcDir.rmdir(src);
                }

                self->d->completedItems.fetch_add(1);
                mutatedSources.append(src);
                mutatedDestinations.append(destPath);
                ++successfulCount;
                QMetaObject::invokeMethod(self, [self, src, destPath]() {
                    if (self) emit self->itemFinished(src, destPath, true, QString());
                }, Qt::QueuedConnection);
                return true;
            }

            // 2. Regular File processing
            // Overwrite never writes into the existing target: it copies into a temporary beside it and replaces
            // the target atomically only after a complete, synced and verified copy (COPY-SAFETY-01).
            const bool replaceTarget = collision == CollisionPolicy::Overwrite;
            if (replaceTarget) {
                const QString refusal = overwriteRefusal(srcInfo, destPath);
                if (!refusal.isEmpty()) {
                    ++failedCount;
                    QMetaObject::invokeMethod(self, [self, src, destPath, refusal]() {
                        if (self) emit self->itemFinished(src, destPath, false, refusal);
                    }, Qt::QueuedConnection);
                    return false;
                }
            }

            const bool sameVol = isSameVolume(src, dstDir);
            if (mode == TransferMode::Move && sameVol && (replaceTarget || !QFileInfo::exists(destPath))) {
                // Fast atomic rename on same volume (under Overwrite it replaces the target in one step)
                QString replaceError;
                bool originalDisplaced = false;
                const bool renamed = replaceTarget
                    ? copy_safety::replaceFileAtomically(src, destPath, &replaceError, &originalDisplaced)
                    : QFile::rename(src, destPath);
                if (originalDisplaced) {
                    // Windows left the original under its backup name and could not move it back: stop here
                    // rather than write a new file at the target's name.
                    ++failedCount;
                    const QString message = QStringLiteral("Could not replace the destination file (%1).").arg(replaceError);
                    QMetaObject::invokeMethod(self, [self, src, destPath, message]() {
                        if (self) emit self->itemFinished(src, destPath, false, message);
                    }, Qt::QueuedConnection);
                    return false;
                }
                if (renamed) {
                    self->d->completedItems.fetch_add(1);
                    self->d->transferredBytes.fetch_add(srcInfo.size());
                    mutatedSources.append(src);
                    mutatedDestinations.append(destPath);
                    ++successfulCount;
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, true, QString());
                    }, Qt::QueuedConnection);
                    return true;
                }
            }

            // Chunked copy (works across volumes and for Copy mode)
            QFile srcFile(src);
            if (!srcFile.open(QIODevice::ReadOnly)) {
                ++failedCount;
                QMetaObject::invokeMethod(self, [self, src, destPath]() {
                    if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Cannot open source file."));
                }, Qt::QueuedConnection);
                return false;
            }

            std::unique_ptr<copy_safety::TemporaryCopy> temporary;
            QFile directFile;
            QFile* destFile = nullptr;
            if (replaceTarget) {
                const QString targetDir = QFileInfo(destPath).absolutePath();
                if (!sweptDirectories.contains(targetDir)) {
                    sweptDirectories.insert(targetDir);
                    copy_safety::removeStaleTemporaries(targetDir);
                }
                temporary = std::make_unique<copy_safety::TemporaryCopy>(destPath);
                if (temporary->isOpen()) destFile = &temporary->file();
            } else {
                directFile.setFileName(destPath);
                if (directFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) destFile = &directFile;
            }
            if (!destFile) {
                ++failedCount;
                QMetaObject::invokeMethod(self, [self, src, destPath]() {
                    if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Cannot open destination file for writing."));
                }, Qt::QueuedConnection);
                return false;
            }
            const QString writePath = destFile->fileName();

            constexpr qint64 ChunkSize = 64 * 1024;
            char buffer[ChunkSize];
            bool copyFailed = false;
            qint64 bytesCopied = 0;

            while (!srcFile.atEnd()) {
                if (!self || self->d->cancelled.load()) {
                    copyFailed = true;
                    break;
                }
                while (self && self->d->paused.load() && !self->d->cancelled.load()) {
                    QThread::msleep(50);
                }

                if (copy_safety::injectFault(copy_safety::FaultPoint::Read, src, bytesCopied)) {
                    copyFailed = true;
                    break;
                }
                const qint64 bytesRead = srcFile.read(buffer, ChunkSize);
                if (bytesRead < 0) {
                    copyFailed = true;
                    break;
                }
                if (copy_safety::injectFault(copy_safety::FaultPoint::Write, writePath, bytesCopied)) {
                    copyFailed = true;
                    break;
                }
                const qint64 bytesWritten = destFile->write(buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                    copyFailed = true;
                    break;
                }
                bytesCopied += bytesWritten;
                self->d->transferredBytes.fetch_add(bytesWritten);
                QMetaObject::invokeMethod(self, [self]() {
                    if (self) emit self->progressChanged();
                }, Qt::QueuedConnection);
                copy_safety::injectFault(copy_safety::FaultPoint::AfterChunk, writePath, bytesCopied);
            }

            if (replaceTarget) {
                const bool synced = !copyFailed
                    && !copy_safety::injectFault(copy_safety::FaultPoint::Sync, writePath, bytesCopied)
                    && copy_safety::syncToDisk(*destFile);
                destFile->close();
                srcFile.close();

                const bool verified = synced && !self->d->cancelled.load()
                    && bytesCopied == srcInfo.size() && QFileInfo(writePath).size() == srcInfo.size();
                QString replaceError;
                bool originalDisplaced = false;
                if (!verified || !copy_safety::replaceFileAtomically(writePath, destPath, &replaceError, &originalDisplaced)) {
                    // The target was never opened: discard the temporary and leave target and source untouched
                    temporary.reset();
                    ++failedCount;
                    const QString message = !verified
                        ? QStringLiteral("Copy verification failed or cancelled. Source and destination preserved.")
                        : originalDisplaced
                            ? QStringLiteral("Could not replace the destination file (%1).").arg(replaceError)
                            : QStringLiteral("Could not replace the destination file (%1). Original kept.").arg(replaceError);
                    QMetaObject::invokeMethod(self, [self, src, destPath, message]() {
                        if (self) emit self->itemFinished(src, destPath, false, message);
                    }, Qt::QueuedConnection);
                    return false;
                }
                temporary->release();
                temporary.reset();
            } else {
                destFile->flush();
                destFile->close();
                srcFile.close();

                // Verification (META-07)
                if (copyFailed || self->d->cancelled.load() || QFileInfo(destPath).size() != srcInfo.size()) {
                    // Verification failed or cancelled: clean up the new destination file, keep source untouched
                    QFile::remove(destPath);
                    ++failedCount;
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Copy verification failed or cancelled. Source preserved."));
                    }, Qt::QueuedConnection);
                    return false;
                }
            }

            // Cross-volume Move: source deletion ONLY AFTER verified copy (META-07)
            if (mode == TransferMode::Move) {
                // Second guard (COPY-SAFETY-01): never delete a source that is, or may be, the file now at the
                // destination, e.g. the same file reached through two drive letters.
                const auto match = copy_safety::compareFileIdentity(src, destPath);
                if (match == copy_safety::FileIdentityMatch::Same) {
                    ++failedCount;
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Source and destination are the same file. Source kept."));
                    }, Qt::QueuedConnection);
                    return false;
                }
                if (match == copy_safety::FileIdentityMatch::Unknown && QFileInfo::exists(src)) {
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, true, QStringLiteral("Copied successfully, but the original source was kept because it could not be confirmed as a separate file."));
                    }, Qt::QueuedConnection);
                } else if (match == copy_safety::FileIdentityMatch::Different && !QFile::remove(src)) {
                    // Could not remove source; report error
                    QMetaObject::invokeMethod(self, [self, src, destPath]() {
                        if (self) emit self->itemFinished(src, destPath, true, QStringLiteral("Copied successfully, but could not delete original source."));
                    }, Qt::QueuedConnection);
                }
            }

            self->d->completedItems.fetch_add(1);
            mutatedSources.append(src);
            mutatedDestinations.append(destPath);
            ++successfulCount;
            QMetaObject::invokeMethod(self, [self, src, destPath]() {
                if (self) emit self->itemFinished(src, destPath, true, QString());
            }, Qt::QueuedConnection);
            return true;
        };

        for (const auto& item : items) {
            if (self->d->cancelled.load()) break;
            if (item.snapshot) {
                // Reported, never substituted; the other members proceed (ASSET-GROUP, failure table).
                const auto state = assets::detail::compareToSnapshot(*item.snapshot);
                if (state != assets::detail::SnapshotState::Same) {
                    const QString dest = clean(QDir(destinationDirectory).filePath(item.targetName));
                    const QString why = state == assets::detail::SnapshotState::Missing
                        ? QStringLiteral("Missing: it no longer exists.")
                        : QStringLiteral("Changed: it changed after you confirmed, so it was left in place.");
                    ++failedCount;
                    self->d->completedItems.fetch_add(1);
                    QMetaObject::invokeMethod(self, [self, source = item.source, dest, why]() {
                        if (self) emit self->itemFinished(source, dest, false, why);
                    }, Qt::QueuedConnection);
                    continue;
                }
            }
            plannedName = item.targetName;
            processItem(item.source, destinationDirectory);
            plannedName.clear();
        }

        if (self) {
            self->d->active.store(false);
            self->d->paused.store(false);
            QMetaObject::invokeMethod(self, [self, batchId, successfulCount, failedCount, mutatedSources, mutatedDestinations, mode]() {
                if (!self) return;
                emit self->activeChanged();
                emit self->pausedChanged();
                emit self->progressChanged();
                emit self->batchFinished(batchId, successfulCount, failedCount);
                if (!mutatedSources.isEmpty()) {
                    emit self->pathsMutated(mutatedSources, mutatedDestinations, mode == TransferMode::Move);
                }
            }, Qt::QueuedConnection);
        }
    });

    return batchId;
}

} // namespace glancelot

