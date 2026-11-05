#pragma once
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <array>
#include <functional>
#include <memory>

// Lead-frozen RESOURCE-LEDGER (docs/contracts/RESOURCE-LEDGER.md). Implemented by packet E1.
// One ledger per process: every image-sized allocation is reserved before it is made, committed when it exists and
// released exactly once on success, cancel, worker death, stale completion or shutdown.
namespace glancelot::resource {
enum class Ledger : quint8 { CpuPixels, WorkerScratch, Transfer, RetainedCache, Thumbnails, GpuTextures };
inline constexpr int kLedgerCount = 6;
enum class Priority : quint8 { Foreground, Visible, Background, Speculative };
enum class Pressure : quint8 { Normal, Elevated, Critical };
enum class Refusal : quint8 { None, InvalidRequest, OwnerQuota, LedgerCeiling, AggregateLimit,
                              PressureShed, TooManyReservations, ShuttingDown };
enum class Enforcement : quint8 { Shadow, Enforce }; // Shadow: always admit, count what would have been refused

inline constexpr qint64 kUnknown = -1;               // an unreadable or unlimited source; never treated as zero
inline constexpr int kMaxLiveReservations = 4096;
inline constexpr int kMaxOwners = 64;
inline constexpr qint64 kDefaultSafetyReserveBytes = 64LL << 20;
inline constexpr qint64 kDefaultFallbackBytes = 1LL << 30;
// One foreground decode at the largest validated size: 256 MiB of pixels plus 2x that in worker scratch.
inline constexpr qint64 kMaxForegroundHeadroomBytes = 768LL << 20;

struct LimitSources {
    qint64 userCapBytes{kUnknown};      // Settings or power profile (E3)
    qint64 osHeadroomBytes{kUnknown};   // safe fraction of OS-available physical memory
    qint64 cgroupLimitBytes{kUnknown};  // minimum over ancestors: memory.max (v2) or memory.limit_in_bytes (v1)
    qint64 overheadBytes{0};            // measured non-image process overhead
    qint64 safetyReserveBytes{kDefaultSafetyReserveBytes};
    qint64 fallbackBytes{kDefaultFallbackBytes}; // when user, OS and cgroup are all unknown
};
// min(known of user, OS, cgroup) - overhead - safetyReserve, floored at 0; fallbackBytes when none is known.
qint64 effectiveLimit(const LimitSources& sources) noexcept;

struct ProbeOptions { QString procRoot{"/proc"}, cgroupRoot{"/sys/fs/cgroup"}; double osFraction{0.40}; };
// Blocking file reads: worker thread only (LimitProbe), never the UI or render thread. Tests pass fake roots.
LimitSources probeSystemLimits(const ProbeOptions& options = {});

struct Thresholds { double reclaimAt{0.80}, recoverAt{0.65}, criticalAt{0.95}; int cooldownMs{2000}; };

struct OwnerId {
    quint32 value{};
    constexpr explicit operator bool() const noexcept { return value != 0; }
    bool operator==(const OwnerId&) const = default;
};

struct Request {
    Ledger ledger{Ledger::CpuPixels};
    Priority priority{Priority::Background};
    OwnerId owner;
    qint64 bytes{};           // checked stride*height (plus staging); >= 0, overflow checked by the caller
    quint64 shareKey{};       // nonzero: physical storage identity (QImage::cacheKey of the detached buffer)
    quint64 generation{};     // the caller's generation; diagnostics only, the ledger never judges staleness
    const char* tag{""};      // static string, diagnostics only (see the contract's decisions)
};

struct ReservationState;
// Move-only. Empty -> Reserved -> Owned -> Released. release() and the destructor are idempotent and thread-safe; the
// bytes leave the ledger exactly once whichever path gets there first.
class Reservation {
public:
    Reservation() = default;
    Reservation(Reservation&&) noexcept;
    Reservation& operator=(Reservation&&) noexcept;   // releases the held reservation first
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    ~Reservation();
    explicit operator bool() const noexcept;          // Reserved or Owned
    bool owned() const noexcept;
    qint64 bytes() const noexcept;                    // what the owner holds (its quota view)
    qint64 charged() const noexcept;                  // what this handle adds to physical totals (0 for a share)
    Ledger ledger() const noexcept;
    Refusal refusal() const noexcept;                 // the reason when empty after tryReserve
    // Publication. actualBytes <= bytes(); the difference returns at once. Growing needs a new reservation.
    // False and no change when empty, released or already owned.
    bool commit(qint64 actualBytes);
    void release() noexcept;
private:
    friend class ResourceLedger;
    std::shared_ptr<ReservationState> state_;
};

class ResourceLedger final : public QObject {
    Q_OBJECT
public:
    static ResourceLedger& instance();                // created on the GUI thread before any window
    OwnerId registerOwner(const QString& diagnosticName);  // an empty OwnerId when kMaxOwners is reached
    void unregisterOwner(OwnerId owner);              // the owner must have no live reservations
    void setOwnerQuota(OwnerId owner, Ledger ledger, qint64 bytes);  // kUnknown = no per-owner quota
    void setLedgerCeiling(Ledger ledger, qint64 bytes);              // kUnknown = aggregate only
    void setAggregateMember(Ledger ledger, bool counts);             // GpuTextures: false in M1, see the contract
    void setForegroundHeadroom(qint64 bytes);         // kept free from non-Foreground requests
    void setLimitSources(const LimitSources& sources);
    void setThresholds(const Thresholds& thresholds);
    void setEnforcement(Enforcement enforcement);
    void notifyExternalPressure(Pressure level);      // OS signal (Windows notification, PSI, memory.events)

    // Never blocks or waits; O(1) under one mutex; any thread, including scene-graph render threads.
    Reservation tryReserve(const Request& request);

    struct LedgerSnapshot { qint64 charged{}, ceiling{kUnknown}, highWater{}; int live{}; quint64 refusals{}, wouldRefuse{}; };
    struct Snapshot {
        std::array<LedgerSnapshot, kLedgerCount> ledgers;
        qint64 effectiveLimit{}, aggregate{}, highWater{};
        Pressure pressure{Pressure::Normal};
        quint64 revision{};                           // increments on every change; UI drops older snapshots
    };
    Snapshot snapshot() const;
    qint64 ownerBytes(OwnerId owner, Ledger ledger) const;

    enum class ReclaimTier : quint8 { Speculative, Hidden, Background, Quality };
    // Runs on the GUI thread, never under the ledger lock, cheapest tier first. Returns the bytes it released through
    // Reservation::release(); it must not allocate or reserve.
    using Reclaimer = std::function<qint64(qint64 targetBytes)>;
    quint64 addReclaimer(ReclaimTier tier, OwnerId owner, Reclaimer reclaimer);
    void removeReclaimer(quint64 token);
    void shutdown();                                  // refuse new reservations; existing ones still release
signals:
    void pressureChanged(glancelot::resource::Pressure level);   // queued, hysteresis applied
    void capacityAvailable(glancelot::resource::Ledger ledger);  // coalesced, emitted outside the lock
private:
    ResourceLedger();
    ~ResourceLedger() override;
    struct Private;
    std::unique_ptr<Private> d;
};
}
