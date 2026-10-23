#include "glancelot/copy_move_service.hpp"
#include "copy_safety.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <QRunnable>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QThread>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
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
QSet<QString>& inFlightTemporaries() {
    static QSet<QString> paths;
    return paths;
}

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
                inFlightTemporaries().insert(path_);
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
        inFlightTemporaries().remove(path_);
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
    const QFileInfo info(targetPath);
    const QString suffix = QString::number(QRandomGenerator::global()->generate64(), 16).rightJustified(16, QLatin1Char('0'));
    return clean(info.dir().filePath(QStringLiteral(".%1.glancelot-%2.part").arg(info.fileName(), suffix)));
}

bool isTemporaryName(const QString& fileName) {
    static const QRegularExpression pattern(QStringLiteral("^\\..+\\.glancelot-[0-9a-f]{16}\\.part$"));
    return pattern.match(fileName).hasMatch();
}

bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error) {
    if (injectFault(FaultPoint::Replace, targetPath, 0)) {
        if (error) *error = QStringLiteral("injected replace failure");
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring from = QDir::toNativeSeparators(replacementPath).toStdWString();
    const std::wstring to = QDir::toNativeSeparators(targetPath).toStdWString();
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = QStringLiteral("Windows error %1").arg(GetLastError());
        return false;
    }
    return true;
#else
    if (::rename(QFile::encodeName(replacementPath).constData(), QFile::encodeName(targetPath).constData()) != 0) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
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
            if (inFlightTemporaries().contains(path)) continue;
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

    explicit Private(CopyMoveService* parent) : q(parent) {}

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

    const quint64 batchId = d->nextBatchId++;
    d->active.store(true);
    d->paused.store(false);
    d->cancelled.store(false);
    emit activeChanged();
    emit pausedChanged();

    QPointer<CopyMoveService> self(this);

    QThreadPool::globalInstance()->start([self, batchId, sourcePaths, destinationDirectory, mode, collision]() {
        if (!self) return;

        // 1. Collect statistics (items and total bytes)
        int totalItemsCount = 0;
        qint64 totalBytesCount = 0;
        for (const auto& path : sourcePaths) {
            collectStats(path, totalItemsCount, totalBytesCount);
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

            const QString targetName = srcInfo.fileName();
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
                const bool renamed = replaceTarget ? copy_safety::replaceFileAtomically(src, destPath)
                                                   : QFile::rename(src, destPath);
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
                if (!verified || !copy_safety::replaceFileAtomically(writePath, destPath, &replaceError)) {
                    // The target was never opened: discard the temporary and leave target and source untouched
                    temporary.reset();
                    ++failedCount;
                    const QString message = verified
                        ? QStringLiteral("Could not replace the destination file (%1). Original kept.").arg(replaceError)
                        : QStringLiteral("Copy verification failed or cancelled. Source and destination preserved.");
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
                if (!QFile::remove(src)) {
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

        for (const auto& path : sourcePaths) {
            if (self->d->cancelled.load()) break;
            processItem(path, destinationDirectory);
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

