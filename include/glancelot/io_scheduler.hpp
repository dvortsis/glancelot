#pragma once
#include <QObject>
#include <QString>
#include <QFileInfo>
#include <memory>
#include <cstdint>

namespace glancelot {

enum class StorageDeviceType {
    Unknown,
    Ssd,
    Rotational, // Mechanical HDD
    Network     // SMB / CIFS / NFS / Remote share
};

enum class StorageProfile {
    Auto,
    Ssd,
    Rotational,
    Network
};

enum class JobPriority {
    Foreground = 0, // High: user interactive image decode
    Thumbnail = 1,  // Medium: filmstrip & grid visible thumbnails
    Prefetch = 2,   // Low: speculative lookahead into cache
    Metadata = 3    // Low: EXIF / directory background scans
};

class IoScheduler : public QObject {
    Q_OBJECT
public:
    static IoScheduler& instance();

    virtual ~IoScheduler() = default;

    // Storage detection & profile
    virtual StorageDeviceType detectStorageType(const QString& path) const = 0;
    virtual StorageProfile profile() const = 0;
    virtual void setProfile(StorageProfile profile) = 0;
    virtual StorageDeviceType effectiveStorageType(const QString& path) const = 0;

    // Concurrency limits by storage type
    virtual int maxConcurrentBackgroundWorkers(const QString& path) const = 0;
    virtual int recommendedThumbnailDeadlineMs(const QString& path) const = 0;
    virtual bool isPrefetchPermitted(const QString& path) const = 0;

    // Admission & memory limits
    virtual bool tryAdmit(quint64 jobId, JobPriority priority, qint64 estimatedBytes, const QString& path) = 0;
    virtual void release(quint64 jobId) = 0;
    virtual void setMemoryLimit(qint64 bytes) = 0;
    virtual qint64 memoryLimit() const = 0;
    virtual qint64 admittedBytes() const = 0;
    virtual int activeJobCount() const = 0;
    virtual int activeForegroundJobs() const = 0;

    // Reset/clear for tests
    virtual void reset() = 0;

    // Fixture / testing hooks: override storage type for a specific path prefix
    virtual void setPathOverride(const QString& pathPrefix, StorageDeviceType type) = 0;
    virtual void clearPathOverrides() = 0;

signals:
    void profileChanged(StorageProfile profile);
    void storageTypeChanged(StorageDeviceType type);
    void admissionThrottled(quint64 jobId, JobPriority priority);
};

inline const char* storageDeviceTypeToString(StorageDeviceType type) {
    switch (type) {
        case StorageDeviceType::Ssd: return "SSD";
        case StorageDeviceType::Rotational: return "HDD";
        case StorageDeviceType::Network: return "Network";
        default: return "Unknown";
    }
}

inline const char* storageProfileToString(StorageProfile profile) {
    switch (profile) {
        case StorageProfile::Auto: return "auto";
        case StorageProfile::Ssd: return "ssd";
        case StorageProfile::Rotational: return "hdd";
        case StorageProfile::Network: return "network";
        default: return "auto";
    }
}

inline StorageProfile parseStorageProfile(const QString& str) {
    const QString s = str.trimmed().toLower();
    if (s == "ssd") return StorageProfile::Ssd;
    if (s == "hdd" || s == "rotational") return StorageProfile::Rotational;
    if (s == "network" || s == "smb" || s == "nfs") return StorageProfile::Network;
    return StorageProfile::Auto;
}

} // namespace glancelot
