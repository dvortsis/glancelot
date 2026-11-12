// E1 (RESOURCE-LEDGER): acceptance tests from docs/contracts/RESOURCE-LEDGER.md, "Acceptance tests E1 must pass".
// Unit tests construct their own ResourceLedger (shutdown() is irreversible). The adoption tests use the process
// instance, as production does, and compare against a baseline snapshot taken at the start of each test.
#include "glancelot/resource_ledger.hpp"

#include <QSignalSpy>
#include <QtTest>
#include <limits>
#include <memory>
#include <vector>

using namespace glancelot::resource;

namespace {
constexpr qint64 MiB = 1LL << 20;
Request req(OwnerId owner, Ledger ledger, qint64 bytes, Priority priority = Priority::Background, quint64 key = 0) {
    Request r;
    r.ledger = ledger; r.priority = priority; r.owner = owner; r.bytes = bytes; r.shareKey = key; r.tag = "test";
    return r;
}
LimitSources userCap(qint64 bytes) {
    LimitSources s;
    s.userCapBytes = bytes; s.safetyReserveBytes = 0;
    return s;
}
qint64 charged(const ResourceLedger& ledger, Ledger which) { return ledger.snapshot().ledgers[int(which)].charged; }
int live(const ResourceLedger& ledger) {
    int total = 0;
    for (const auto& l : ledger.snapshot().ledgers) total += l.live;
    return total;
}
QImage frame(int width, int height, QRgb colour) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}
struct BudgetScope {
    qint64 saved = glancelot::DisplayManager::imageBudgetBytes();
    explicit BudgetScope(qint64 bytes) { glancelot::DisplayManager::setImageBudgetBytes(bytes); }
    ~BudgetScope() { glancelot::DisplayManager::setImageBudgetBytes(saved); }
};
} // namespace

class ResourceLedgerTests : public QObject {
    Q_OBJECT
private slots:
    void refusalPrecedenceChangesNothing() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("owner");
        QVERIFY(owner);
        QCOMPARE(ledger.tryReserve(req(OwnerId{999}, Ledger::CpuPixels, 1)).refusal(), Refusal::InvalidRequest);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, -1)).refusal(), Refusal::InvalidRequest);
        ledger.setLimitSources(userCap(100));
        ledger.setLedgerCeiling(Ledger::CpuPixels, 50);
        ledger.setOwnerQuota(owner, Ledger::CpuPixels, 10);
        const auto before = ledger.snapshot();
        // Quota, then ceiling, then aggregate: the first failure wins.
        auto refused = ledger.tryReserve(req(owner, Ledger::CpuPixels, 200, Priority::Foreground));
        QVERIFY(!refused);
        QCOMPARE(refused.refusal(), Refusal::OwnerQuota);
        ledger.setOwnerQuota(owner, Ledger::CpuPixels, kUnknown);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, 200, Priority::Foreground)).refusal(), Refusal::LedgerCeiling);
        ledger.setLedgerCeiling(Ledger::CpuPixels, kUnknown);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, 200, Priority::Foreground)).refusal(), Refusal::AggregateLimit);
        const auto after = ledger.snapshot();
        QCOMPARE(after.aggregate, before.aggregate);
        QCOMPARE(after.ledgers[int(Ledger::CpuPixels)].charged, 0);
        QCOMPARE(after.ledgers[int(Ledger::CpuPixels)].live, 0);
        QCOMPARE(after.ledgers[int(Ledger::CpuPixels)].refusals, quint64(3));
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 0);
        ledger.shutdown();
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, 1)).refusal(), Refusal::ShuttingDown);
        QCOMPARE(ledger.tryReserve(req(OwnerId{999}, Ledger::CpuPixels, 1)).refusal(), Refusal::InvalidRequest);
    }
    void overflowAndNegativeSizesAreInvalid() {
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(std::numeric_limits<qint64>::max()));
        const auto owner = ledger.registerOwner("owner");
        auto one = ledger.tryReserve(req(owner, Ledger::Transfer, 1, Priority::Foreground));
        QVERIFY(one);
        auto huge = ledger.tryReserve(req(owner, Ledger::Transfer, std::numeric_limits<qint64>::max(), Priority::Foreground));
        QCOMPARE(huge.refusal(), Refusal::InvalidRequest);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Transfer, std::numeric_limits<qint64>::min())).refusal(), Refusal::InvalidRequest);
        QCOMPARE(charged(ledger, Ledger::Transfer), 1);
    }
    void commitShrinkReturnsTheDifference() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("owner");
        auto r = ledger.tryReserve(req(owner, Ledger::CpuPixels, 100));
        QVERIFY(r && !r.owned());
        QVERIFY(!r.commit(101));   // growing needs a new reservation
        QVERIFY(r.commit(60));
        QVERIFY(r.owned());
        QCOMPARE(r.bytes(), 60);
        QCOMPARE(r.charged(), 60);
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 60);
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 60);
        QVERIFY(!r.commit(10));    // already owned: no change
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 60);
    }
    void commitWithShareKeyPublishesAndCommitOntoAKeyShares() {
        ResourceLedger ledger;
        const auto a = ledger.registerOwner("a"), b = ledger.registerOwner("b");
        auto first = ledger.tryReserve(req(a, Ledger::CpuPixels, 100));
        QVERIFY(first.commit(100, 42));
        auto later = ledger.tryReserve(req(b, Ledger::CpuPixels, 100, Priority::Visible, 42));
        QVERIFY(later && later.owned());
        QCOMPARE(later.charged(), 0);
        QCOMPARE(later.bytes(), 100);
        QCOMPARE(ledger.ownerBytes(b, Ledger::CpuPixels), 100);   // the owner view counts the share
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 100);        // physical: once
        auto racer = ledger.tryReserve(req(b, Ledger::CpuPixels, 100));
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 200);
        QVERIFY(racer.commit(100, 42));                          // the buffer already exists: becomes a share
        QCOMPARE(racer.charged(), 0);
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 100);
        QCOMPARE(ledger.ownerBytes(b, Ledger::CpuPixels), 100);   // deduplicated per owner
    }
    void releaseHappensExactlyOnce() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("owner");
        {
            auto r = ledger.tryReserve(req(owner, Ledger::WorkerScratch, 10));
            r.release(); r.release();
            QCOMPARE(charged(ledger, Ledger::WorkerScratch), 0);
            QCOMPARE(live(ledger), 0);
        }
        auto a = ledger.tryReserve(req(owner, Ledger::WorkerScratch, 10));
        auto b = ledger.tryReserve(req(owner, Ledger::WorkerScratch, 20));
        a = std::move(b);   // releases a's 10 first
        QCOMPARE(charged(ledger, Ledger::WorkerScratch), 20);
        Reservation moved(std::move(a));
        QVERIFY(!a);
        a.release();        // a moved-from handle releases nothing
        QCOMPARE(charged(ledger, Ledger::WorkerScratch), 20);
        { Reservation scoped = std::move(moved); }
        QCOMPARE(charged(ledger, Ledger::WorkerScratch), 0);
        QCOMPARE(ledger.snapshot().ledgers[int(Ledger::WorkerScratch)].live, 0);
        QCOMPARE(ledger.ownerBytes(owner, Ledger::WorkerScratch), 0);
    }
    void liveReservationsAreBounded() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("owner");
        std::vector<Reservation> held;
        held.reserve(kMaxLiveReservations);
        for (int i = 0; i < kMaxLiveReservations; ++i) held.push_back(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1)));
        QVERIFY(held.back());
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1)).refusal(), Refusal::TooManyReservations);
        held.pop_back();
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1)));
        held.clear();
        QCOMPARE(live(ledger), 0);
    }
    void shareChargesOnceAndTheChargeMigrates() {
        ResourceLedger ledger;
        const auto cache = ledger.registerOwner("decoded-cache"), controller = ledger.registerOwner("controller");
        auto cached = ledger.tryReserve(req(cache, Ledger::RetainedCache, 100, Priority::Speculative, 7));
        QCOMPARE(charged(ledger, Ledger::RetainedCache), 100);
        const auto aggregate = ledger.snapshot().aggregate;
        // The current image shares the cached buffer: charged once, to the most essential holder.
        auto current = ledger.tryReserve(req(controller, Ledger::CpuPixels, 100, Priority::Foreground, 7));
        QCOMPARE(charged(ledger, Ledger::CpuPixels), 100);
        QCOMPARE(charged(ledger, Ledger::RetainedCache), 0);
        QCOMPARE(ledger.snapshot().aggregate, aggregate);
        QCOMPARE(current.charged(), 100);
        QCOMPARE(cached.charged(), 0);
        current.release();   // the charge moves back under the same lock: totals never dip and double
        QCOMPARE(charged(ledger, Ledger::RetainedCache), 100);
        QCOMPARE(ledger.snapshot().aggregate, aggregate);
        QCOMPARE(ledger.snapshot().highWater, aggregate);
        cached.release();
        QCOMPARE(ledger.snapshot().aggregate, 0);
    }
    void ownerViewIsDeduplicatedPerOwner() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("presentation");
        ledger.setOwnerQuota(owner, Ledger::CpuPixels, 100);
        auto current = ledger.tryReserve(req(owner, Ledger::CpuPixels, 100, Priority::Foreground, 9));
        auto previous = ledger.tryReserve(req(owner, Ledger::CpuPixels, 100, Priority::Background, 9));
        auto reference = ledger.tryReserve(req(owner, Ledger::CpuPixels, 100, Priority::Visible, 9));
        QVERIFY(previous && reference);   // the same buffer fits the quota once
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 100);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, 1, Priority::Foreground, 10)).refusal(), Refusal::OwnerQuota);
        current.release(); previous.release();
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 100);
        reference.release();
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 0);
    }
    void effectiveLimitCombinesKnownSources() {
        const qint64 user = 3000 * MiB, os = 2000 * MiB, cgroup = 1000 * MiB;
        for (int mask = 0; mask < 8; ++mask) {
            LimitSources s;
            s.userCapBytes = (mask & 1) ? user : kUnknown;
            s.osHeadroomBytes = (mask & 2) ? os : kUnknown;
            s.cgroupLimitBytes = (mask & 4) ? cgroup : kUnknown;
            s.overheadBytes = 100 * MiB;
            qint64 base = kDefaultFallbackBytes;
            if (mask) {
                base = std::numeric_limits<qint64>::max();
                if (mask & 1) base = std::min(base, user);
                if (mask & 2) base = std::min(base, os);
                if (mask & 4) base = std::min(base, cgroup);
            }
            QCOMPARE(effectiveLimit(s), base - 100 * MiB - kDefaultSafetyReserveBytes);
        }
        LimitSources tiny;
        tiny.cgroupLimitBytes = 10 * MiB;
        QCOMPARE(effectiveLimit(tiny), 0);   // floored at zero
        LimitSources zero;
        zero.userCapBytes = 0;
        QCOMPARE(effectiveLimit(zero), 0);   // a known zero is not unknown
        QCOMPARE(effectiveLimit(LimitSources{}), kMaxForegroundHeadroomBytes);
    }
    void foregroundHeadroomIsKeptFree_data() {
        QTest::addColumn<qint64>("limit");
        QTest::addColumn<qint64>("headroom");
        QTest::newRow("above twice the cap") << 4 * kMaxForegroundHeadroomBytes << kMaxForegroundHeadroomBytes;
        QTest::newRow("below twice the cap") << kMaxForegroundHeadroomBytes << kMaxForegroundHeadroomBytes / 2;
    }
    void foregroundHeadroomIsKeptFree() {
        QFETCH(qint64, limit);
        QFETCH(qint64, headroom);
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(limit));
        Thresholds noCooldown; noCooldown.cooldownMs = 0;   // reaching 100 % below raises pressure; let it fall at once
        ledger.setThresholds(noCooldown);
        const auto owner = ledger.registerOwner("owner");
        const qint64 room = limit - headroom;
        auto background = ledger.tryReserve(req(owner, Ledger::Transfer, room, Priority::Background));
        QVERIFY(background);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Transfer, 1, Priority::Visible)).refusal(), Refusal::AggregateLimit);
        auto foreground = ledger.tryReserve(req(owner, Ledger::Transfer, headroom, Priority::Foreground));
        QVERIFY(foreground);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Transfer, 1, Priority::Foreground)).refusal(), Refusal::AggregateLimit);
        foreground.release(); background.release();
        ledger.setForegroundHeadroom(headroom * 2);   // a profile may lower it, never raise it
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Transfer, room, Priority::Background)));
        ledger.setForegroundHeadroom(0);
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Transfer, limit, Priority::Background)));
    }
    void largestDecodeFitsWithEverySourceUnknown() {
        ResourceLedger ledger;
        QCOMPARE(ledger.snapshot().effectiveLimit, kMaxForegroundHeadroomBytes);
        const auto owner = ledger.registerOwner("controller");
        const qint64 pixels = 256 * MiB;
        auto source = ledger.tryReserve(req(owner, Ledger::WorkerScratch, pixels, Priority::Foreground));
        auto converted = ledger.tryReserve(req(owner, Ledger::WorkerScratch, pixels, Priority::Foreground));
        auto transfer = ledger.tryReserve(req(owner, Ledger::Transfer, pixels, Priority::Foreground));
        auto copy = ledger.tryReserve(req(owner, Ledger::CpuPixels, pixels, Priority::Foreground));
        QVERIFY(source && converted && transfer && copy);
        QCOMPARE(ledger.snapshot().aggregate, 4 * pixels);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::CpuPixels, 1, Priority::Foreground)).refusal(), Refusal::AggregateLimit);
    }
    void migrationOverACeilingRefusesNothingAndStartsReclaim() {
        ResourceLedger ledger;
        const auto cache = ledger.registerOwner("decoded-cache"), controller = ledger.registerOwner("controller");
        auto cached = ledger.tryReserve(req(cache, Ledger::RetainedCache, 100, Priority::Speculative, 5));
        auto current = ledger.tryReserve(req(controller, Ledger::CpuPixels, 100, Priority::Foreground, 5));
        ledger.setLedgerCeiling(Ledger::RetainedCache, 50);   // charged 0 there: the current image pays
        QSignalSpy pressure(&ledger, &ResourceLedger::pressureChanged);
        qint64 asked = 0;
        int calls = 0;
        ledger.addReclaimer(ResourceLedger::ReclaimTier::Speculative, cache, [&](qint64 target) {
            ++calls; asked = target; const auto freed = cached.bytes(); cached.release(); return freed;
        });
        current.release();   // the charge moves to RetainedCache, over its ceiling: allowed, nothing refused
        QVERIFY(cached);
        QCOMPARE(charged(ledger, Ledger::RetainedCache), 100);
        QCOMPARE(calls, 0);  // reclaim runs later, on the ledger's thread, outside the lock
        QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 10000);
        QCOMPARE(asked, 50);
        QCOMPARE(charged(ledger, Ledger::RetainedCache), 0);
        QCOMPARE(pressure.count(), 0);
    }
    void retiredOwnerRefusesAndGoesAfterItsLastRelease() {
        ResourceLedger ledger;
        std::vector<OwnerId> owners;
        for (int i = 0; i < kMaxOwners; ++i) owners.push_back(ledger.registerOwner(QStringLiteral("o%1").arg(i)));
        QVERIFY(owners.back());
        QVERIFY(!ledger.registerOwner("one too many"));
        QCOMPARE(ledger.tryReserve(req(OwnerId{}, Ledger::CpuPixels, 1)).refusal(), Refusal::InvalidRequest);
        auto late = ledger.tryReserve(req(owners[0], Ledger::Transfer, 10));
        ledger.unregisterOwner(owners[0]);
        QCOMPARE(ledger.tryReserve(req(owners[0], Ledger::Transfer, 1)).refusal(), Refusal::InvalidRequest);
        QVERIFY(!ledger.registerOwner("still full"));   // the retired owner stays until its last handle releases
        late.release();
        const auto replacement = ledger.registerOwner("replacement");
        QVERIFY(replacement);
        QVERIFY(!(replacement == owners[0]));
        QCOMPARE(charged(ledger, Ledger::Transfer), 0);
    }
    void pressureRisesAtOnceAndFallsOnlyBelowRecoverAfterCooldown() {
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(1000));
        ledger.setForegroundHeadroom(0);
        Thresholds t; t.cooldownMs = 60 * 60 * 1000;
        ledger.setThresholds(t);
        QSignalSpy spy(&ledger, &ResourceLedger::pressureChanged);
        const auto owner = ledger.registerOwner("owner");
        auto base = ledger.tryReserve(req(owner, Ledger::Transfer, 600, Priority::Foreground));
        auto bump = ledger.tryReserve(req(owner, Ledger::Transfer, 250, Priority::Foreground));   // 85 %
        QCOMPARE(ledger.snapshot().pressure, Pressure::Elevated);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
        QCOMPARE(spy.at(0).at(0).value<Pressure>(), Pressure::Elevated);
        auto spike = ledger.tryReserve(req(owner, Ledger::Transfer, 110, Priority::Foreground));     // 96 %
        QCOMPARE(ledger.snapshot().pressure, Pressure::Critical);
        spike.release();   // 85 %: no fall above recoverAt
        QCOMPARE(ledger.snapshot().pressure, Pressure::Critical);
        bump.release();    // 60 %, below recoverAt, but inside the cooldown
        QCOMPARE(ledger.snapshot().pressure, Pressure::Critical);
        t.cooldownMs = 50;
        ledger.setThresholds(t);
        QTRY_COMPARE_WITH_TIMEOUT(ledger.snapshot().pressure, Pressure::Normal, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 10000);
        QCOMPARE(spy.last().at(0).value<Pressure>(), Pressure::Normal);
        // An external signal raises it; it falls when the signal clears and the aggregate is low.
        ledger.notifyExternalPressure(Pressure::Critical);
        QCOMPARE(ledger.snapshot().pressure, Pressure::Critical);
        ledger.notifyExternalPressure(Pressure::Normal);
        QTRY_COMPARE_WITH_TIMEOUT(ledger.snapshot().pressure, Pressure::Normal, 10000);
    }
    void pressureShedsByPriority() {
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(1000));
        ledger.setForegroundHeadroom(0);
        const auto owner = ledger.registerOwner("owner");
        auto held = ledger.tryReserve(req(owner, Ledger::CpuPixels, 850, Priority::Foreground));
        QCOMPARE(ledger.snapshot().pressure, Pressure::Elevated);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1, Priority::Speculative)).refusal(), Refusal::PressureShed);
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1, Priority::Background)));
        ledger.notifyExternalPressure(Pressure::Critical);
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1, Priority::Background)).refusal(), Refusal::PressureShed);
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Thumbnails, 1, Priority::Visible)));
        QVERIFY(ledger.tryReserve(req(owner, Ledger::Transfer, 150, Priority::Foreground)));   // ignores pressure
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Transfer, 151, Priority::Foreground)).refusal(), Refusal::AggregateLimit);
        // Outside the aggregate, pressure does not apply.
        QVERIFY(ledger.tryReserve(req(owner, Ledger::GpuTextures, 5000, Priority::Speculative)));
    }
    void reclaimersRunOutsideTheLockInTierOrder() {
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(1000));
        ledger.setForegroundHeadroom(0);
        const auto owner = ledger.registerOwner("owner");
        std::vector<Reservation> speculative, hidden;
        for (int i = 0; i < 2; ++i) speculative.push_back(ledger.tryReserve(req(owner, Ledger::RetainedCache, 100, Priority::Speculative)));
        for (int i = 0; i < 4; ++i) hidden.push_back(ledger.tryReserve(req(owner, Ledger::Thumbnails, 100, Priority::Background)));
        auto current = ledger.tryReserve(req(owner, Ledger::CpuPixels, 200, Priority::Foreground));
        QStringList order;
        Refusal fromReclaimer = Refusal::None;
        const auto drain = [&](std::vector<Reservation>& pool, const QString& name, qint64 target) {
            order << name;
            (void)ledger.snapshot();   // takes the ledger lock: deadlocks if the reclaimer ran under it
            fromReclaimer = ledger.tryReserve(req(owner, Ledger::Transfer, 1, Priority::Foreground)).refusal();
            qint64 freed = 0;
            while (!pool.empty() && freed < target) { freed += pool.back().bytes(); pool.back().release(); pool.pop_back(); }
            return freed;
        };
        ledger.addReclaimer(ResourceLedger::ReclaimTier::Background, owner, [&](qint64 t) { return drain(hidden, "background", t); });
        ledger.addReclaimer(ResourceLedger::ReclaimTier::Hidden, owner, [&](qint64 t) { return drain(hidden, "hidden", t); });
        const auto quality = ledger.addReclaimer(ResourceLedger::ReclaimTier::Quality, owner, [&](qint64) { order << "quality"; return qint64(0); });
        ledger.addReclaimer(ResourceLedger::ReclaimTier::Speculative, owner, [&](qint64 t) { return drain(speculative, "speculative", t); });
        ledger.removeReclaimer(quality);
        auto spike = ledger.tryReserve(req(owner, Ledger::CpuPixels, 100, Priority::Foreground));   // 900, Elevated
        QVERIFY(spike);
        QTRY_VERIFY_WITH_TIMEOUT(!order.isEmpty() && ledger.snapshot().aggregate <= 650, 10000);
        QCOMPARE(order.first(), QStringLiteral("speculative"));
        QCOMPARE(order.size(), 2);   // 900 - 650 = 250: both cached frames (200), then one hidden thumbnail
        QCOMPARE(order.at(1), QStringLiteral("hidden"));
        QCOMPARE(fromReclaimer, Refusal::InvalidRequest);   // a reclaimer that reserves is rejected
        QVERIFY(current);            // the current image is never reclaimed
    }
    void shadowAdmitsAndCountsWhatWouldBeRefused() {
        ResourceLedger ledger;
        ledger.setEnforcement(Enforcement::Shadow);
        ledger.setLimitSources(userCap(100));
        const auto owner = ledger.registerOwner("owner");
        ledger.setOwnerQuota(owner, Ledger::CpuPixels, 10);
        auto over = ledger.tryReserve(req(owner, Ledger::CpuPixels, 500, Priority::Foreground));
        QVERIFY(over);
        const auto snap = ledger.snapshot();
        QCOMPARE(snap.ledgers[int(Ledger::CpuPixels)].wouldRefuse, quint64(1));
        QCOMPARE(snap.ledgers[int(Ledger::CpuPixels)].refusals, quint64(0));
        QCOMPARE(snap.ledgers[int(Ledger::CpuPixels)].charged, 500);
        QCOMPARE(ledger.tryReserve(req(OwnerId{77}, Ledger::CpuPixels, 1)).refusal(), Refusal::InvalidRequest);
    }
    void shutdownRefusesNewWorkAndInFlightWorkReleases() {
        ResourceLedger ledger;
        const auto owner = ledger.registerOwner("io");
        auto inFlight = ledger.tryReserve(req(owner, Ledger::Transfer, 64, Priority::Foreground));
        auto worker = std::make_unique<Reservation>(ledger.tryReserve(req(owner, Ledger::WorkerScratch, 128, Priority::Foreground)));
        ledger.shutdown();
        QCOMPARE(ledger.tryReserve(req(owner, Ledger::Transfer, 1, Priority::Foreground)).refusal(), Refusal::ShuttingDown);
        QVERIFY(inFlight.commit(64));
        inFlight.release();
        worker.reset();
        QCOMPARE(live(ledger), 0);
        QCOMPARE(ledger.snapshot().aggregate, 0);
    }
    void handlesMayOutliveTheirLedger() {
        Reservation survivor;
        {
            ResourceLedger ledger;
            const auto owner = ledger.registerOwner("late");
            survivor = ledger.tryReserve(req(owner, Ledger::Transfer, 10));
            QVERIFY(survivor);
        }
        QVERIFY(survivor);
        survivor.release();
        QVERIFY(!survivor);
    }
    void reservationsAreThreadSafe() {
        ResourceLedger ledger;
        ledger.setLimitSources(userCap(1LL << 40));
        const auto owner = ledger.registerOwner("threads");
        std::vector<std::unique_ptr<QThread>> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back(QThread::create([&ledger, owner, t] {
                for (int i = 0; i < 2000; ++i) {
                    auto r = ledger.tryReserve(req(owner, Ledger(quint8(t % 3)), 1 + i % 17, Priority::Foreground, (i % 5) ? 0 : quint64(1 + i % 3)));
                    if (i % 2) { Reservation moved = std::move(r); }
                }
            }));
            threads.back()->start();
        }
        for (auto& thread : threads) QVERIFY(thread->wait(30000));
        QCOMPARE(live(ledger), 0);
        QCOMPARE(ledger.snapshot().aggregate, 0);
        QCOMPARE(ledger.ownerBytes(owner, Ledger::CpuPixels), 0);
    }
};

QTEST_MAIN(ResourceLedgerTests)
#include "resource_ledger_tests.moc"
