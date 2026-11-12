#include "glancelot/resource_ledger.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <limits>
#include <map>

// E1 (RESOURCE-LEDGER, docs/contracts/RESOURCE-LEDGER.md). One mutex guards every counter. Handles keep the core
// alive through a shared_ptr, so a Reservation may outlive its ResourceLedger (static services, render threads).
// The lock is never held while user code runs: reclaimers and signal receivers run from a queued call on the
// ledger's thread, after the lock is dropped.
namespace glancelot::resource {
namespace {
constexpr int index(Ledger ledger) { return int(ledger); }
// Charge attribution for shared storage: the most essential holder pays (lower is more essential).
constexpr int essentialRank(Ledger ledger) {
    switch (ledger) {
    case Ledger::CpuPixels: return 0;
    case Ledger::RetainedCache: return 1;
    case Ledger::Thumbnails: return 2;
    case Ledger::Transfer: return 3;
    case Ledger::WorkerScratch: return 4;
    case Ledger::GpuTextures: return 5;
    }
    return 6;
}
bool validLedger(Ledger ledger) { return quint8(ledger) < kLedgerCount; }
bool addOverflows(qint64 a, qint64 b) { return b > 0 && a > std::numeric_limits<qint64>::max() - b; }
thread_local int t_reclaimerDepth = 0;   // a reclaimer that reserves is rejected
using Clock = std::chrono::steady_clock;
} // namespace

qint64 effectiveLimit(const LimitSources& sources) noexcept {
    qint64 base = kUnknown;
    for (const qint64 value : {sources.userCapBytes, sources.osHeadroomBytes, sources.cgroupLimitBytes}) {
        if (value < 0) continue;   // kUnknown (or any negative) is never treated as zero
        base = base < 0 ? value : std::min(base, value);
    }
    if (base < 0) base = std::max<qint64>(0, sources.fallbackBytes);
    const qint64 overhead = std::max<qint64>(0, sources.overheadBytes);
    const qint64 safety = std::max<qint64>(0, sources.safetyReserveBytes);
    if (base <= overhead) return 0;
    base -= overhead;
    return base <= safety ? 0 : base - safety;
}

struct Core;

struct ReservationState {
    enum class Phase : quint8 { Reserved, Owned, Released };
    std::shared_ptr<Core> core;
    Ledger ledger{Ledger::CpuPixels};
    Priority priority{Priority::Background};
    quint32 owner{};
    qint64 bytes{};
    qint64 charged{};
    quint64 key{};          // published share key (0 until published)
    bool shared{};          // a holder of a keyed buffer entry
    Phase phase{Phase::Reserved};
    Refusal refusal{Refusal::None};
};

struct Core {
    struct OwnerKey { int count{}; qint64 bytes{}; };
    struct Owner {
        QString name;
        bool retired{};
        int handles{};
        std::array<qint64, kLedgerCount> bytes{};
        std::array<qint64, kLedgerCount> quota{kUnknown, kUnknown, kUnknown, kUnknown, kUnknown, kUnknown};
        std::array<QHash<quint64, OwnerKey>, kLedgerCount> keys;
    };
    struct Buffer {   // one physical buffer, charged once to its most essential holder
        qint64 bytes{};
        ReservationState* charger{};
        QList<ReservationState*> holders;
    };
    struct LedgerData {
        qint64 charged{}, ceiling{kUnknown}, highWater{};
        int live{};
        quint64 refusals{}, wouldRefuse{};
        bool member{true};
    };
    struct ReclaimerEntry { ResourceLedger::ReclaimTier tier; quint32 owner; ResourceLedger::Reclaimer fn; };

    mutable QMutex mutex;
    ResourceLedger* q{};                       // cleared under the lock by ~ResourceLedger
    QHash<quint32, Owner> owners;
    quint32 nextOwner{1};
    std::array<QHash<quint64, Buffer>, 2> buffers;   // [0] CPU-side ledgers, [1] GpuTextures (never shared with CPU)
    std::array<LedgerData, kLedgerCount> ledgers;
    int live{};
    bool shutdown{};
    Enforcement enforcement{Enforcement::Enforce};
    LimitSources sources;
    qint64 limit{effectiveLimit(LimitSources{})};
    qint64 headroomCap{kUnknown};
    qint64 headroom{};
    qint64 highWater{};
    Thresholds thresholds;
    Pressure level{Pressure::Normal};
    Pressure external{Pressure::Normal};
    Clock::time_point lastRise{};
    Clock::time_point recheckAt{Clock::time_point::max()};   // the earliest queued cooldown recheck
    bool reclaimQueued{};
    quint64 revision{};
    quint8 capacityPending{};                  // one bit per ledger, coalesced
    std::map<quint64, ReclaimerEntry> reclaimers;
    quint64 nextReclaimer{1};

    Core() {
        ledgers[index(Ledger::GpuTextures)].member = false;   // M1: outside the aggregate (contract, GPU membership)
        recomputeHeadroom();
    }

    qint64 aggregateLocked() const {
        qint64 total = 0;
        for (const auto& data : ledgers) if (data.member) total += data.charged;
        return total;
    }
    void recomputeHeadroom() {
        qint64 value = std::min(kMaxForegroundHeadroomBytes, limit / 2);
        if (headroomCap >= 0) value = std::min(value, headroomCap);   // a profile may lower it, never raise it
        headroom = std::max<qint64>(0, value);
    }
    void noteChangeLocked() {
        ++revision;
        const auto aggregate = aggregateLocked();
        highWater = std::max(highWater, aggregate);
        for (auto& data : ledgers) data.highWater = std::max(data.highWater, data.charged);
    }
    static Ledger ledgerOf(int i) { return Ledger(quint8(i)); }

    // Owner view: shares count, one buffer once per owner and ledger.
    void ownerAdd(Owner& owner, Ledger ledger, quint64 key, qint64 bytes) {
        auto& total = owner.bytes[index(ledger)];
        if (!key) { total += bytes; return; }
        auto& entry = owner.keys[index(ledger)][key];
        if (entry.count++ == 0) { entry.bytes = bytes; total += bytes; }
    }
    void ownerRemove(Owner& owner, Ledger ledger, quint64 key, qint64 bytes) {
        auto& total = owner.bytes[index(ledger)];
        if (!key) { total -= bytes; return; }
        auto& keys = owner.keys[index(ledger)];
        auto it = keys.find(key);
        if (it == keys.end()) return;
        if (--it->count == 0) { total -= it->bytes; keys.erase(it); }
    }
    qint64 ownerDelta(const Owner& owner, Ledger ledger, quint64 key, qint64 bytes) const {
        if (key && owner.keys[index(ledger)].contains(key)) return 0;
        return bytes;
    }

    void moveCharge(Buffer& buffer, ReservationState* to) {
        if (buffer.charger == to) return;
        if (buffer.charger) {
            ledgers[index(buffer.charger->ledger)].charged -= buffer.bytes;
            buffer.charger->charged = 0;
        }
        buffer.charger = to;
        if (to) {
            ledgers[index(to->ledger)].charged += buffer.bytes;
            to->charged = buffer.bytes;
            const auto& data = ledgers[index(to->ledger)];
            // A move may push the inheriting ledger over its ceiling: allowed, refuses nothing, starts reclaim.
            if (data.ceiling >= 0 && data.charged > data.ceiling) queueReclaimLocked();
        }
    }
    void joinBuffer(Buffer& buffer, ReservationState* state) {
        state->shared = true;
        state->charged = 0;
        buffer.holders.append(state);
        if (!buffer.charger || essentialRank(state->ledger) < essentialRank(buffer.charger->ledger)) moveCharge(buffer, state);
    }
    void leaveBuffer(ReservationState* state) {
        auto& map = buffers[state->ledger == Ledger::GpuTextures ? 1 : 0];
        auto it = map.find(state->key);
        if (it == map.end()) return;
        auto& buffer = *it;
        buffer.holders.removeOne(state);
        if (buffer.holders.isEmpty()) {
            moveCharge(buffer, nullptr);
            map.erase(it);
            return;
        }
        if (buffer.charger == state) {
            ReservationState* next = buffer.holders.first();
            for (auto* holder : buffer.holders)
                if (essentialRank(holder->ledger) < essentialRank(next->ledger)) next = holder;
            // Under the same lock: totals never dip and double.
            moveCharge(buffer, next);
        }
    }

    void postLocked(std::function<void(ResourceLedger*)> fn) {
        if (!q) return;
        QMetaObject::invokeMethod(q, [ledger = q, fn = std::move(fn)] { fn(ledger); }, Qt::QueuedConnection);
    }
    void queueCapacityLocked(Ledger ledger) {
        const quint8 bit = quint8(1u << index(ledger));
        const bool first = capacityPending == 0;
        capacityPending |= bit;
        if (!first) return;
        postLocked([core = this](ResourceLedger* ledgerObject) {
            QMutexLocker locker(&core->mutex);
            const auto pending = std::exchange(core->capacityPending, quint8(0));
            locker.unlock();
            for (int i = 0; i < kLedgerCount; ++i)
                if (pending & (1u << i)) emit ledgerObject->capacityAvailable(ledgerOf(i));
        });
    }
    void queueReclaimLocked() {
        if (std::exchange(reclaimQueued, true)) return;
        postLocked([core = this](ResourceLedger*) { runReclaim(core); });
    }
    static void runReclaim(Core* core);   // on the ledger thread, from a call posted to it

    // Hysteresis. Rises at once; falls only below recoverAt and only after cooldownMs since the last rise.
    void evaluatePressureLocked() {
        const qint64 aggregate = aggregateLocked();
        const double ratio = limit > 0 ? double(aggregate) / double(limit) : (aggregate > 0 ? 1e9 : 0.0);
        Pressure target = Pressure::Normal;
        if (ratio >= thresholds.criticalAt) target = Pressure::Critical;
        else if (ratio >= thresholds.reclaimAt) target = Pressure::Elevated;
        target = std::max(target, external);
        if (target > level) {
            level = target;
            lastRise = Clock::now();
            ++revision;
            postLocked([level = target](ResourceLedger* ledgerObject) { emit ledgerObject->pressureChanged(level); });
            queueReclaimLocked();
            return;
        }
        if (target == level) return;
        // target < level. The level drops only below recoverAt; above it, it holds whatever it reached.
        const Pressure fallTo = ratio < thresholds.recoverAt ? external : level;
        if (fallTo >= level) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastRise).count();
        if (elapsed < thresholds.cooldownMs) {
            const auto deadline = lastRise + std::chrono::milliseconds(thresholds.cooldownMs);
            if (deadline < recheckAt) {   // a shorter cooldown replaces a later recheck
                recheckAt = deadline;
                const int wait = int(std::max<qint64>(1, thresholds.cooldownMs - elapsed));
                postLocked([wait, core = this](ResourceLedger* ledgerObject) {
                    QTimer::singleShot(wait, ledgerObject, [core] {
                        QMutexLocker locker(&core->mutex);
                        if (Clock::now() >= core->recheckAt) core->recheckAt = Clock::time_point::max();
                        core->evaluatePressureLocked();
                    });
                });
            }
            return;
        }
        level = fallTo;
        ++revision;
        postLocked([level = fallTo](ResourceLedger* ledgerObject) { emit ledgerObject->pressureChanged(level); });
    }

    void releaseLocked(ReservationState* state) {
        if (state->phase == ReservationState::Phase::Released) return;
        state->phase = ReservationState::Phase::Released;
        --live;
        auto& data = ledgers[index(state->ledger)];
        --data.live;
        auto ownerIt = owners.find(state->owner);
        if (ownerIt != owners.end()) {
            ownerRemove(*ownerIt, state->ledger, state->key, state->bytes);
            if (--ownerIt->handles == 0 && ownerIt->retired) owners.erase(ownerIt);
        }
        if (state->shared) leaveBuffer(state);
        else data.charged -= state->charged;
        state->charged = 0;
        noteChangeLocked();
        queueCapacityLocked(state->ledger);
        evaluatePressureLocked();
    }
};

void Core::runReclaim(Core* core) {
    std::vector<std::pair<ResourceLedger::ReclaimTier, ResourceLedger::Reclaimer>> ordered;
    {
        QMutexLocker locker(&core->mutex);
        core->reclaimQueued = false;
        for (const auto& [token, entry] : core->reclaimers) ordered.emplace_back(entry.tier, entry.fn);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return quint8(a.first) < quint8(b.first); });
    const auto need = [&core] {
        QMutexLocker locker(&core->mutex);
        qint64 bytes = 0;
        if (core->level != Pressure::Normal)
            bytes = core->aggregateLocked() - qint64(core->thresholds.recoverAt * double(core->limit));
        for (const auto& data : core->ledgers)
            if (data.ceiling >= 0) bytes = std::max(bytes, data.charged - data.ceiling);
        return bytes;
    };
    for (auto& [tier, fn] : ordered) {
        const qint64 target = need();
        if (target <= 0) break;
        ++t_reclaimerDepth;
        fn(target);   // outside the lock, on the ledger's thread
        --t_reclaimerDepth;
    }
}

struct ResourceLedger::Private {
    std::shared_ptr<Core> core = std::make_shared<Core>();
};

// ---- Reservation ----
Reservation::Reservation(Reservation&& other) noexcept : state_(std::move(other.state_)) {}
Reservation& Reservation::operator=(Reservation&& other) noexcept {
    if (this != &other) {
        release();
        state_ = std::move(other.state_);
    }
    return *this;
}
Reservation::~Reservation() { release(); }
Reservation::operator bool() const noexcept {
    if (!state_ || !state_->core) return false;
    QMutexLocker locker(&state_->core->mutex);
    return state_->phase != ReservationState::Phase::Released;
}
bool Reservation::owned() const noexcept {
    if (!state_ || !state_->core) return false;
    QMutexLocker locker(&state_->core->mutex);
    return state_->phase == ReservationState::Phase::Owned;
}
qint64 Reservation::bytes() const noexcept {
    if (!state_ || !state_->core) return 0;
    QMutexLocker locker(&state_->core->mutex);
    return state_->phase == ReservationState::Phase::Released ? 0 : state_->bytes;
}
qint64 Reservation::charged() const noexcept {
    if (!state_ || !state_->core) return 0;
    QMutexLocker locker(&state_->core->mutex);
    return state_->charged;
}
Ledger Reservation::ledger() const noexcept { return state_ ? state_->ledger : Ledger::CpuPixels; }
Refusal Reservation::refusal() const noexcept { return state_ ? state_->refusal : Refusal::None; }

bool Reservation::commit(qint64 actualBytes, quint64 shareKey) {
    if (!state_ || !state_->core) return false;
    auto& core = *state_->core;
    QMutexLocker locker(&core.mutex);
    auto* state = state_.get();
    if (state->phase != ReservationState::Phase::Reserved || actualBytes < 0 || actualBytes > state->bytes) return false;
    auto ownerIt = core.owners.find(state->owner);
    const qint64 returned = state->bytes - actualBytes;
    auto& data = core.ledgers[index(state->ledger)];
    data.charged -= returned;
    if (ownerIt != core.owners.end()) core.ownerRemove(*ownerIt, state->ledger, 0, returned);
    state->bytes = actualBytes;
    state->charged = actualBytes;
    bool freed = returned > 0;
    if (shareKey) {
        if (ownerIt != core.owners.end()) {   // the owner view moves from unkeyed to keyed (deduplicated)
            core.ownerRemove(*ownerIt, state->ledger, 0, actualBytes);
            core.ownerAdd(*ownerIt, state->ledger, shareKey, actualBytes);
        }
        state->key = shareKey;
        auto& map = core.buffers[state->ledger == Ledger::GpuTextures ? 1 : 0];
        auto it = map.find(shareKey);
        if (it != map.end()) {
            // The buffer already exists: this handle becomes a share and its charge returns at once.
            data.charged -= actualBytes;
            state->charged = 0;
            core.joinBuffer(*it, state);
            freed = freed || actualBytes > 0;
        } else {
            Core::Buffer buffer;
            buffer.bytes = actualBytes;
            buffer.charger = state;
            buffer.holders.append(state);
            state->shared = true;
            map.insert(shareKey, buffer);
        }
    }
    state->phase = ReservationState::Phase::Owned;
    core.noteChangeLocked();
    if (freed) core.queueCapacityLocked(state->ledger);
    core.evaluatePressureLocked();
    return true;
}

void Reservation::release() noexcept {
    if (!state_) return;
    if (auto core = state_->core) {
        QMutexLocker locker(&core->mutex);
        core->releaseLocked(state_.get());
    }
}

// ---- ResourceLedger ----
ResourceLedger::ResourceLedger() : d(std::make_unique<Private>()) {
    QMutexLocker locker(&d->core->mutex);
    d->core->q = this;
}
ResourceLedger::~ResourceLedger() {
    QMutexLocker locker(&d->core->mutex);
    d->core->q = nullptr;   // handles may outlive the object; they keep the core
    d->core->reclaimers.clear();
}

ResourceLedger& ResourceLedger::instance() {
    // Leaked on purpose: services with static lifetime release their handles after main() returns.
    static ResourceLedger* ledger = [] {
        auto* created = new ResourceLedger;
        if (auto* app = QCoreApplication::instance(); app && created->thread() != app->thread())
            created->moveToThread(app->thread());
        return created;
    }();
    return *ledger;
}

OwnerId ResourceLedger::registerOwner(const QString& diagnosticName) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    if (core.owners.size() >= kMaxOwners || core.nextOwner == 0) return {};
    const quint32 id = core.nextOwner++;
    Core::Owner owner;
    owner.name = diagnosticName;
    core.owners.insert(id, owner);
    return OwnerId{id};
}
void ResourceLedger::unregisterOwner(OwnerId owner) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    auto it = core.owners.find(owner.value);
    if (it == core.owners.end()) return;
    if (it->handles == 0) core.owners.erase(it);
    else it->retired = true;
}
void ResourceLedger::setOwnerQuota(OwnerId owner, Ledger ledger, qint64 bytes) {
    if (!validLedger(ledger)) return;
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    auto it = core.owners.find(owner.value);
    if (it == core.owners.end()) return;
    const auto previous = it->quota[index(ledger)];
    it->quota[index(ledger)] = bytes < 0 ? kUnknown : bytes;
    if (previous >= 0 && (bytes < 0 || bytes > previous)) core.queueCapacityLocked(ledger);
}
void ResourceLedger::setLedgerCeiling(Ledger ledger, qint64 bytes) {
    if (!validLedger(ledger)) return;
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    auto& data = core.ledgers[index(ledger)];
    const auto previous = data.ceiling;
    data.ceiling = bytes < 0 ? kUnknown : bytes;
    ++core.revision;
    if (previous >= 0 && (bytes < 0 || bytes > previous)) core.queueCapacityLocked(ledger);
    if (data.ceiling >= 0 && data.charged > data.ceiling) core.queueReclaimLocked();
}
void ResourceLedger::setAggregateMember(Ledger ledger, bool counts) {
    if (!validLedger(ledger)) return;
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.ledgers[index(ledger)].member = counts;
    core.noteChangeLocked();
    core.evaluatePressureLocked();
}
void ResourceLedger::setForegroundHeadroom(qint64 bytes) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.headroomCap = bytes < 0 ? kUnknown : bytes;
    core.recomputeHeadroom();
    ++core.revision;
}
void ResourceLedger::setLimitSources(const LimitSources& sources) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    const auto previous = core.limit;
    core.sources = sources;
    core.limit = effectiveLimit(sources);
    core.recomputeHeadroom();
    ++core.revision;
    if (core.limit > previous)
        for (int i = 0; i < kLedgerCount; ++i) if (core.ledgers[i].member) core.queueCapacityLocked(Core::ledgerOf(i));
    core.evaluatePressureLocked();
}
void ResourceLedger::setThresholds(const Thresholds& thresholds) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.thresholds = thresholds;
    ++core.revision;
    core.evaluatePressureLocked();
}
void ResourceLedger::setEnforcement(Enforcement enforcement) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.enforcement = enforcement;
    ++core.revision;
}
void ResourceLedger::notifyExternalPressure(Pressure level) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.external = level;
    core.evaluatePressureLocked();
}

Reservation ResourceLedger::tryReserve(const Request& request) {
    auto core = d->core;
    Reservation result;
    result.state_ = std::make_shared<ReservationState>();
    auto* state = result.state_.get();
    state->ledger = request.ledger;
    state->priority = request.priority;
    state->owner = request.owner.value;
    state->bytes = request.bytes;
    const auto refuse = [&](Refusal why) {
        state->refusal = why;
        state->phase = ReservationState::Phase::Released;
        return std::move(result);
    };
    if (t_reclaimerDepth > 0) return refuse(Refusal::InvalidRequest);   // reclaimers must not reserve
    QMutexLocker locker(&core->mutex);
    // 1. Validate.
    auto ownerIt = core->owners.find(request.owner.value);
    if (!validLedger(request.ledger) || ownerIt == core->owners.end() || ownerIt->retired || request.bytes < 0)
        return refuse(Refusal::InvalidRequest);
    auto& data = core->ledgers[index(request.ledger)];
    if (addOverflows(data.charged, request.bytes) || addOverflows(ownerIt->bytes[index(request.ledger)], request.bytes)
        || addOverflows(core->aggregateLocked(), request.bytes))
        return refuse(Refusal::InvalidRequest);
    if (core->live >= kMaxLiveReservations) { ++data.refusals; return refuse(Refusal::TooManyReservations); }
    if (core->shutdown) { ++data.refusals; return refuse(Refusal::ShuttingDown); }
    // 2. Share hit.
    auto& map = core->buffers[request.ledger == Ledger::GpuTextures ? 1 : 0];
    auto bufferIt = request.shareKey ? map.find(request.shareKey) : map.end();
    const bool share = bufferIt != map.end();
    Refusal wouldRefuse = Refusal::None;
    // 3. Owner quota (shares count, deduplicated per owner).
    const qint64 quota = ownerIt->quota[index(request.ledger)];
    const qint64 delta = core->ownerDelta(*ownerIt, request.ledger, request.shareKey, request.bytes);
    if (quota >= 0 && ownerIt->bytes[index(request.ledger)] + delta > quota) wouldRefuse = Refusal::OwnerQuota;
    if (!share && wouldRefuse == Refusal::None) {
        // 4. Ledger ceiling (physical).
        if (data.ceiling >= 0 && data.charged + request.bytes > data.ceiling) wouldRefuse = Refusal::LedgerCeiling;
        // 5. Aggregate, pressure and foreground headroom.
        else if (data.member) {
            const bool foreground = request.priority == Priority::Foreground;
            const qint64 aggregate = core->aggregateLocked();
            if (!foreground && ((core->level >= Pressure::Elevated && request.priority == Priority::Speculative)
                                || (core->level == Pressure::Critical && request.priority == Priority::Background)))
                wouldRefuse = Refusal::PressureShed;
            else if (aggregate + request.bytes > core->limit) wouldRefuse = Refusal::AggregateLimit;
            else if (!foreground && aggregate + request.bytes > core->limit - core->headroom)
                wouldRefuse = Refusal::AggregateLimit;
        }
    }
    if (wouldRefuse != Refusal::None) {
        if (core->enforcement == Enforcement::Enforce) {
            ++data.refusals;
            ++core->revision;
            return refuse(wouldRefuse);
        }
        ++data.wouldRefuse;   // Shadow: admit, count what would have been refused
    }
    state->core = core;
    state->phase = request.shareKey ? ReservationState::Phase::Owned : ReservationState::Phase::Reserved;
    ++core->live;
    ++data.live;
    ++ownerIt->handles;
    core->ownerAdd(*ownerIt, request.ledger, request.shareKey, request.bytes);
    if (share) {
        state->key = request.shareKey;
        core->joinBuffer(*bufferIt, state);
    } else if (request.shareKey) {
        // A holder of a buffer that already exists (Request::shareKey): published at once, so the next holder shares.
        state->key = request.shareKey;
        state->shared = true;
        state->charged = request.bytes;
        data.charged += request.bytes;
        Core::Buffer buffer;
        buffer.bytes = request.bytes;
        buffer.charger = state;
        buffer.holders.append(state);
        map.insert(request.shareKey, buffer);
    } else {
        state->charged = request.bytes;
        data.charged += request.bytes;
    }
    core->noteChangeLocked();
    core->evaluatePressureLocked();
    return result;
}

ResourceLedger::Snapshot ResourceLedger::snapshot() const {
    const auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    Snapshot snap;
    for (int i = 0; i < kLedgerCount; ++i) {
        const auto& data = core.ledgers[i];
        snap.ledgers[i] = {data.charged, data.ceiling, data.highWater, data.live, data.refusals, data.wouldRefuse};
    }
    snap.effectiveLimit = core.limit;
    snap.aggregate = core.aggregateLocked();
    snap.highWater = core.highWater;
    snap.pressure = core.level;
    snap.revision = core.revision;
    return snap;
}
qint64 ResourceLedger::ownerBytes(OwnerId owner, Ledger ledger) const {
    if (!validLedger(ledger)) return 0;
    const auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    const auto it = core.owners.constFind(owner.value);
    return it == core.owners.cend() ? 0 : it->bytes[index(ledger)];
}

quint64 ResourceLedger::addReclaimer(ReclaimTier tier, OwnerId owner, Reclaimer reclaimer) {
    if (!reclaimer) return 0;
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    const auto token = core.nextReclaimer++;
    core.reclaimers.emplace(token, Core::ReclaimerEntry{tier, owner.value, std::move(reclaimer)});
    return token;
}
void ResourceLedger::removeReclaimer(quint64 token) {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.reclaimers.erase(token);
}
void ResourceLedger::shutdown() {
    auto& core = *d->core;
    QMutexLocker locker(&core.mutex);
    core.shutdown = true;
    ++core.revision;
}
} // namespace glancelot::resource
