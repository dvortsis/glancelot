#include "glancelot/copy_move_service.hpp"
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
#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
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
            const bool sameVol = isSameVolume(src, dstDir);
            if (mode == TransferMode::Move && sameVol && !QFileInfo::exists(destPath)) {
                // Fast atomic rename on same volume
                if (QFile::rename(src, destPath)) {
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

            QFile destFile(destPath);
            if (!destFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                ++failedCount;
                QMetaObject::invokeMethod(self, [self, src, destPath]() {
                    if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Cannot open destination file for writing."));
                }, Qt::QueuedConnection);
                return false;
            }

            constexpr qint64 ChunkSize = 64 * 1024;
            char buffer[ChunkSize];
            bool copyFailed = false;

            while (!srcFile.atEnd()) {
                if (!self || self->d->cancelled.load()) {
                    copyFailed = true;
                    break;
                }
                while (self && self->d->paused.load() && !self->d->cancelled.load()) {
                    QThread::msleep(50);
                }

                const qint64 bytesRead = srcFile.read(buffer, ChunkSize);
                if (bytesRead < 0) {
                    copyFailed = true;
                    break;
                }
                const qint64 bytesWritten = destFile.write(buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                    copyFailed = true;
                    break;
                }
                self->d->transferredBytes.fetch_add(bytesWritten);
                QMetaObject::invokeMethod(self, [self]() {
                    if (self) emit self->progressChanged();
                }, Qt::QueuedConnection);
            }

            destFile.flush();
            destFile.close();
            srcFile.close();

            // Verification (META-07)
            if (copyFailed || self->d->cancelled.load() || QFileInfo(destPath).size() != srcInfo.size()) {
                // Verification failed or cancelled: clean up destination file, keep source untouched
                QFile::remove(destPath);
                ++failedCount;
                QMetaObject::invokeMethod(self, [self, src, destPath]() {
                    if (self) emit self->itemFinished(src, destPath, false, QStringLiteral("Copy verification failed or cancelled. Source preserved."));
                }, Qt::QueuedConnection);
                return false;
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

