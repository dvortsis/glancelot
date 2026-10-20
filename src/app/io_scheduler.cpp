#include "glancelot/io_scheduler.hpp"
#include <QDir>
#include <QMutex>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/statvfs.h>
#include <fstream>
#endif

namespace glancelot {

quint64 allocateIoJobId() {
    // Separate opaque reservations from legacy selection/wire ordinals while
    // callers migrate; these IDs are never serialized as thumbnail request IDs.
    static std::atomic<quint64> next{quint64(1)<<63};
    auto value=next.load(std::memory_order_relaxed);
    while(value&&!next.compare_exchange_weak(value,value==std::numeric_limits<quint64>::max()?0:value+1,
                                            std::memory_order_relaxed)){}
    return value;
}

class IoSchedulerImpl : public IoScheduler {
public:
    IoSchedulerImpl() = default;

    StorageDeviceType detectStorageType(const QString& path) const override {
        QMutexLocker locker(&mutex_);
        return detectStorageTypeLocked(path);
    }

    StorageProfile profile() const override {
        QMutexLocker locker(&mutex_);
        return profile_;
    }

    void setProfile(StorageProfile profile) override {
        QMutexLocker locker(&mutex_);
        if (profile_ != profile) {
            profile_ = profile;
            locker.unlock();
            emit profileChanged(profile);
        }
    }

    StorageDeviceType effectiveStorageType(const QString& path) const override {
        QMutexLocker locker(&mutex_);
        if (profile_ == StorageProfile::Ssd) return StorageDeviceType::Ssd;
        if (profile_ == StorageProfile::Rotational) return StorageDeviceType::Rotational;
        if (profile_ == StorageProfile::Network) return StorageDeviceType::Network;
        return detectStorageTypeLocked(path);
    }

    int maxConcurrentBackgroundWorkers(const QString& path) const override {
        const auto type = effectiveStorageType(path);
        switch (type) {
            case StorageDeviceType::Ssd: return 2;
            case StorageDeviceType::Rotational: return 1;
            case StorageDeviceType::Network: return 1;
            default: return 2;
        }
    }

    int recommendedThumbnailDeadlineMs(const QString& path) const override {
        const auto type = effectiveStorageType(path);
        switch (type) {
            case StorageDeviceType::Network: return 3000;
            case StorageDeviceType::Rotational: return 8000;
            case StorageDeviceType::Ssd: return 5000;
            default: return 5000;
        }
    }

    bool isPrefetchPermitted(const QString& path) const override {
        QMutexLocker locker(&mutex_);
        if (activeForeground_ > 0) return false;

        const auto type = effectiveStorageTypeLocked(path);
        if (type == StorageDeviceType::Network) {
            return false;
        }
        if (type == StorageDeviceType::Rotational && !activeJobs_.isEmpty()) {
            return false;
        }
        return true;
    }

    bool tryAdmit(quint64 jobId, JobPriority priority, qint64 estimatedBytes, const QString& path) override {
        QMutexLocker locker(&mutex_);
        if (jobId == 0 || estimatedBytes<0 || activeJobs_.contains(jobId) ||
            estimatedBytes>std::numeric_limits<qint64>::max()-admittedBytes_) return false;

        if (priority == JobPriority::Foreground) {
            if (memoryLimit_ > 0 && admittedBytes_ + estimatedBytes > memoryLimit_ && admittedBytes_ > 0) {
                locker.unlock();
                emit admissionThrottled(jobId, priority);
                return false;
            }
            admittedBytes_ += estimatedBytes;
            activeJobs_.insert(jobId, {priority, estimatedBytes, path});
            activeForeground_++;
            return true;
        }

        if (priority == JobPriority::Prefetch && activeForeground_ > 0) {
            locker.unlock();
            emit admissionThrottled(jobId, priority);
            return false;
        }

        const auto type = effectiveStorageTypeLocked(path);
        int maxWorkers = (type == StorageDeviceType::Ssd) ? 2 : 1;
        int activeBackground = activeJobs_.size() - activeForeground_;
        if (activeBackground >= maxWorkers) {
            locker.unlock();
            emit admissionThrottled(jobId, priority);
            return false;
        }

        if (memoryLimit_ > 0 && admittedBytes_ + estimatedBytes > memoryLimit_) {
            locker.unlock();
            emit admissionThrottled(jobId, priority);
            return false;
        }

        admittedBytes_ += estimatedBytes;
        activeJobs_.insert(jobId, {priority, estimatedBytes, path});
        return true;
    }

    void release(quint64 jobId) override {
        QMutexLocker locker(&mutex_);
        auto it = activeJobs_.find(jobId);
        if (it != activeJobs_.end()) {
            if (it->priority == JobPriority::Foreground) {
                if (activeForeground_ > 0) activeForeground_--;
            }
            admittedBytes_ = (std::max)(qint64(0), admittedBytes_ - it->bytes);
            activeJobs_.erase(it);
            locker.unlock();notifyAdmissionAvailable();
        }
    }

    void setMemoryLimit(qint64 bytes) override {
        QMutexLocker locker(&mutex_);
        const auto previous=memoryLimit_;memoryLimit_ = bytes;
        locker.unlock();
        if(bytes<=0||(previous>0&&bytes>previous))notifyAdmissionAvailable();
    }

    qint64 memoryLimit() const override {
        QMutexLocker locker(&mutex_);
        return memoryLimit_;
    }

    qint64 admittedBytes() const override {
        QMutexLocker locker(&mutex_);
        return admittedBytes_;
    }

    int activeJobCount() const override {
        QMutexLocker locker(&mutex_);
        return activeJobs_.size();
    }

    int activeForegroundJobs() const override {
        QMutexLocker locker(&mutex_);
        return activeForeground_;
    }

    void reset() override {
        QMutexLocker locker(&mutex_);
        profile_ = StorageProfile::Auto;
        pathOverrides_.clear();
        activeJobs_.clear();
        admittedBytes_ = 0;
        activeForeground_ = 0;
        memoryLimit_ = 256LL * 1024 * 1024;
    }

    void setPathOverride(const QString& pathPrefix, StorageDeviceType type) override {
        QMutexLocker locker(&mutex_);
        pathOverrides_.insert(QDir::cleanPath(pathPrefix).toLower(), type);
    }

    void clearPathOverrides() override {
        QMutexLocker locker(&mutex_);
        pathOverrides_.clear();
    }

private:
    struct ActiveJob {
        JobPriority priority;
        qint64 bytes;
        QString path;
    };

    StorageDeviceType detectStorageTypeLocked(const QString& path) const {
        if (path.isEmpty()) return StorageDeviceType::Unknown;

        const QString clean = QDir::cleanPath(path).toLower();
        for (auto it = pathOverrides_.cbegin(); it != pathOverrides_.cend(); ++it) {
            if (clean.startsWith(it.key())) {
                return it.value();
            }
        }

        if (clean.startsWith("//") || clean.startsWith("\\\\") || path.startsWith("//") || path.startsWith("\\\\")) {
            return StorageDeviceType::Network;
        }

#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(path);
        if (native.length() >= 2 && native[1] == QLatin1Char(':')) {
            const QString root = native.left(3).endsWith('\\') ? native.left(3) : native.left(2) + "\\";
            const UINT driveType = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
            if (driveType == DRIVE_REMOTE) {
                return StorageDeviceType::Network;
            }
            if (driveType == DRIVE_CDROM) {
                return StorageDeviceType::Rotational;
            }

            const QString volume = QStringLiteral("\\\\.\\") + native.left(2);
            HANDLE hDevice = CreateFileW(reinterpret_cast<LPCWSTR>(volume.utf16()),
                                         0,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL,
                                         OPEN_EXISTING,
                                         0,
                                         NULL);
            if (hDevice != INVALID_HANDLE_VALUE) {
                STORAGE_PROPERTY_QUERY query{};
                query.PropertyId = StorageDeviceSeekPenaltyProperty;
                query.QueryType = PropertyStandardQuery;
                DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
                DWORD bytesReturned = 0;
                BOOL success = DeviceIoControl(hDevice,
                                               IOCTL_STORAGE_QUERY_PROPERTY,
                                               &query,
                                               sizeof(query),
                                               &descriptor,
                                               sizeof(descriptor),
                                               &bytesReturned,
                                               NULL);
                CloseHandle(hDevice);
                if (success) {
                    return descriptor.IncursSeekPenalty ? StorageDeviceType::Rotational : StorageDeviceType::Ssd;
                }
            }
        }
#elif defined(Q_OS_LINUX)
        struct statvfs buf;
        QByteArray utf8 = path.toUtf8();
        if (statvfs(utf8.constData(), &buf) == 0) {
            if (buf.f_type == 0xFF534D42 || buf.f_type == 0x6969 || buf.f_type == 0xFE534D42) {
                return StorageDeviceType::Network;
            }
        }
#endif
        return StorageDeviceType::Ssd;
    }

    StorageDeviceType effectiveStorageTypeLocked(const QString& path) const {
        if (profile_ == StorageProfile::Ssd) return StorageDeviceType::Ssd;
        if (profile_ == StorageProfile::Rotational) return StorageDeviceType::Rotational;
        if (profile_ == StorageProfile::Network) return StorageDeviceType::Network;
        return detectStorageTypeLocked(path);
    }

    void notifyAdmissionAvailable() {
        if(notificationQueued_.exchange(true,std::memory_order_relaxed))return;
        QMetaObject::invokeMethod(this,[this]{notificationQueued_.store(false,std::memory_order_relaxed);emit admissionAvailable();},Qt::QueuedConnection);
    }
    std::atomic_bool notificationQueued_{};
    mutable QMutex mutex_;
    StorageProfile profile_{StorageProfile::Auto};
    QMap<QString, StorageDeviceType> pathOverrides_;
    QMap<quint64, ActiveJob> activeJobs_;
    qint64 admittedBytes_{0};
    qint64 memoryLimit_{256LL * 1024 * 1024};
    int activeForeground_{0};
};

IoScheduler& IoScheduler::instance() {
    static IoSchedulerImpl s_instance;
    return s_instance;
}

} // namespace glancelot
