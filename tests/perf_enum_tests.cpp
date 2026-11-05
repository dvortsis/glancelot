#include "glancelot/directory_entry_model.hpp"
#include "glancelot/perf_trace.hpp"
#include "perf_enum_test_support.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <memory>
#include <random>
#include <thread>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

// A1 PERF-ENUM + F016. Generated, disposable folders only. Every wait is a condition with a deadline.
namespace {
constexpr int kTenThousand = 10000, kHundredThousand = 100000;
constexpr qint64 kGuiGapBudgetMs = 250; // the unchanged BUILD-PLAN GUI budget
constexpr int kPage = 1000, kPagedLimit = 3000;

// Continuation in pages small enough to exercise cheaply. Entries arrive backwards, so each page lands above the last.
struct PagedFolder {
    FakeFolder folder;
    std::unique_ptr<SeamScope> seam;
    explicit PagedFolder(int total) {
        folder.total = total;
        folder.name = [total](int i) { return QString("f%1.png").arg(total - i, 7, 10, QChar('0')); };
    }
    void install() {
        seam = std::make_unique<SeamScope>(&folder);
        directory_scan::testSeam.tuning.entryStep = kPage; directory_scan::testSeam.tuning.entryLimit = kPagedLimit;
    }
};
bool listed(const DirectoryEntryModel& model, int rows) {
    return QTest::qWaitFor([&] { return !model.loading() && model.count() == rows; }, 10000);
}
struct Memory { qint64 workingSetMb{}, peakMb{}; };
Memory memoryNow() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters))
        return {qint64(counters.WorkingSetSize >> 20), qint64(counters.PeakWorkingSetSize >> 20)};
#endif
    return {};
}

// Mixed media as the strategy document asks: folders, hidden files, images, other files, long and non-ASCII names.
bool generate(const QString& directory, int total) {
    QDir root(directory);
    const int folders = std::max(1, total / 200);
    for (int i = 0; i < folders; ++i) if (!root.mkdir(QString("folder%1").arg(i))) return false;
    for (int i = folders; i < total; ++i) {
        const QString name = i % 97 == 0 ? QString("café-%1.png").arg(i)
                           : i % 89 == 0 ? QString("%1-%2.jpg").arg(QString(120, QChar('l'))).arg(i)
                           : i % 4 == 0 ? QString("doc%1.txt").arg(i)
                                        : QString("img%1.png").arg(i);
        QFile file(root.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
    }
    return true;
}

struct Trace {
    qint64 firstRowMs{-1}, fullCountMs{-1}, definitiveMs{-1}, settledMs{-1}, worstGapMs{}, sortMs{}, enumerateMs{}, publishMs{};
    int resets{}, layouts{}, inserts{}, largestInsert{};
    bool partialSeen{};
};
QString describe(const char* name, const Trace& t) {
    return QString("PERF_ENUM_TRACE %1 first_row_ms=%2 full_count_ms=%3 definitive_ms=%14 settled_ms=%4 worst_gui_gap_ms=%5 enumerate_ms=%6 sort_ms=%7 publish_ms=%8 "
                   "resets=%9 layouts=%10 inserts=%11 largest_insert=%12 partial_seen=%13")
        .arg(name).arg(t.firstRowMs).arg(t.fullCountMs).arg(t.settledMs).arg(t.worstGapMs).arg(t.enumerateMs).arg(t.sortMs).arg(t.publishMs)
        .arg(t.resets).arg(t.layouts).arg(t.inserts).arg(t.largestInsert).arg(t.partialSeen).arg(t.definitiveMs);
}
// Holds the first scan to reach it between its definitive order and its identity probes; later scans pass.
struct ProbeHold {
    std::shared_ptr<std::atomic_bool> holding = std::make_shared<std::atomic_bool>(true), entered = std::make_shared<std::atomic_bool>(false),
                                      cancelled = std::make_shared<std::atomic_bool>(false);
    void install() {
        directory_scan::testSeam.beforeProbes = [holding = holding, entered = entered, cancelled = cancelled](std::stop_token stop) {
            if (entered->exchange(true)) return;
            while (holding->load() && !stop.stop_requested()) std::this_thread::yield();
            if (stop.stop_requested()) cancelled->store(true);
        };
    }
    void release() { holding->store(false); }
};
QStringList names(const DirectoryEntryModel& model) {
    QStringList out;
    for (int i = 0; i < model.count(); ++i) out << model.data(model.index(i), DirectoryEntryModel::NameRole).toString();
    return out;
}
// Lists `directory` once and records what a user would see. The heartbeat bounds every GUI-thread stall.
class Tracer : public QObject {
public:
    Tracer(DirectoryEntryModel& watched, int expectedRows) : model(watched), expected(expectedRows) {
        connect(&model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
            ++trace.inserts; trace.largestInsert = std::max(trace.largestInsert, last - first + 1); observe();
        });
        connect(&model, &QAbstractItemModel::modelReset, this, [this] { ++trace.resets; observe(); });
        connect(&model, &QAbstractItemModel::layoutChanged, this, [this] { ++trace.layouts; });
        connect(&model, &DirectoryEntryModel::countChanged, this, [this] { observe(); });
        connect(&model, &DirectoryEntryModel::arrivingChanged, this, [this] { if (!model.arriving() && trace.definitiveMs < 0) trace.definitiveMs = clock.elapsed(); });
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, this, [this] {
            const auto now = clock.elapsed(); trace.worstGapMs = std::max(trace.worstGapMs, now - previous); previous = now;
        });
    }
    void begin() { glancelot::perf::Recorder::instance().drain(); clock.start(); previous = 0; heartbeat.start(); }
    void finish() {
        heartbeat.stop(); trace.settledMs = clock.elapsed();
        for (const auto& span : glancelot::perf::Recorder::instance().drain()) {
            const qint64 ms = (span.endNs - span.startNs) / 1000000;
            if (span.stage == glancelot::perf::Stage::Enumerate) trace.enumerateMs += ms;
            else if (span.stage == glancelot::perf::Stage::Sort) trace.sortMs += ms;
            else if (span.stage == glancelot::perf::Stage::Publish) trace.publishMs = std::max(trace.publishMs, ms);
        }
    }
    DirectoryEntryModel& model; const int expected; Trace trace;
    QElapsedTimer clock; qint64 previous{}; QTimer heartbeat;
private:
    void observe() {
        const int count = model.count();
        if (count > 0 && trace.firstRowMs < 0) trace.firstRowMs = clock.elapsed();
        if (count >= expected && trace.fullCountMs < 0) trace.fullCountMs = clock.elapsed();
        if (count > 0 && count < expected && model.arriving()) trace.partialSeen = true;
    }
};
}

class PerfEnumTests : public QObject {
    Q_OBJECT
    QTemporaryDir ten, hundred;
    bool tenReady{}, hundredReady{};
    // Generated on first use, so the seam tests stay fast to run on their own.
    bool ensure(QTemporaryDir& dir, bool& ready, int total) {
        if (ready) return true;
        QElapsedTimer setup; setup.start();
        ready = generate(dir.path(), total);
        qInfo() << "generated" << total << "entries in" << setup.elapsed() << "ms";
        return ready;
    }
    void listAndReport(const char* name, const QString& directory, int expected, Trace& out) {
        DirectoryEntryModel model; model.setActive(true);
        Tracer tracer(model, expected);
        tracer.begin();
        model.setDirectory(directory);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == expected, 120000);
        tracer.finish();
        out = tracer.trace;
        qInfo().noquote() << describe(name, out);
    }
private slots:
    void initTestCase() {
        glancelot::perf::Recorder::instance().setEnabled(true);
        QVERIFY(ten.isValid()); QVERIFY(hundred.isValid());
    }
    void cleanupTestCase() { glancelot::perf::Recorder::instance().setEnabled(false); }

    // 10k and 100k mixed-folder traces (PERF-ENUM exit evidence). The GUI budget is the unchanged one.
    void trace10k() {
        QVERIFY(ensure(ten, tenReady, kTenThousand));
        Trace first, second;
        listAndReport("10k-cold", ten.path(), kTenThousand, first);
        if (QTest::currentTestFailed()) return;
        listAndReport("10k-warm", ten.path(), kTenThousand, second);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(std::max(first.worstGapMs, second.worstGapMs) < kGuiGapBudgetMs, "10k listing stalled the GUI thread");
        // R1: useful content does not wait for per-file identity probes. On base the first row took 1.5 to 1.9 s here.
        QVERIFY2(second.firstRowMs < 500, qPrintable(QString("10k first row took %1 ms").arg(second.firstRowMs)));
        QVERIFY2(second.definitiveMs < 500, qPrintable(QString("10k definitive order took %1 ms").arg(second.definitiveMs)));
        QVERIFY(second.definitiveMs <= second.settledMs);
    }
    void trace100k() {
        QVERIFY(ensure(hundred, hundredReady, kHundredThousand));
        Trace first, second;
        listAndReport("100k-cold", hundred.path(), kHundredThousand, first);
        if (QTest::currentTestFailed()) return;
        listAndReport("100k-warm", hundred.path(), kHundredThousand, second);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(std::max(first.worstGapMs, second.worstGapMs) < kGuiGapBudgetMs, "100k listing stalled the GUI thread");
        // F016: rows show while the folder is still being read, in deltas rather than one publication.
        QVERIFY2(second.partialSeen, "no partial rows were visible before the 100k listing finished");
        QVERIFY2(second.firstRowMs < second.settledMs, "the first row only arrived with the finished listing");
        QVERIFY2(second.largestInsert < kHundredThousand / 2, "rows arrived in one very large delta");
    }
    // Rows already on screen keep their relative order, and the identity index stays right, while later rows arrive.
    void arrivedRowsKeepTheirOrderUntilTheDefinitiveOrder() {
        QVERIFY(ensure(hundred, hundredReady, kHundredThousand));
        DirectoryEntryModel model; model.setActive(true);
        QStringList sample; int checks = 0, disordered = 0, misindexed = 0;
        const auto check = [&] {
            if (!model.arriving() || model.count() == 0) return;
            if (sample.isEmpty()) for (int row = 0; row < std::min(model.count(), 64); ++row) sample << model.pathAt(row);
            int last = -1;
            for (const auto& path : sample) {
                const int row = model.rowOf(path);
                ++checks;
                if (row <= last) ++disordered;
                if (model.pathAt(row) != path) ++misindexed;
                last = row;
            }
        };
        connect(&model, &DirectoryEntryModel::countChanged, this, check);
        model.setDirectory(hundred.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == kHundredThousand, 120000);
        QVERIFY2(checks > 0, "nothing arrived before the listing finished");
        QCOMPARE(disordered, 0); QCOMPARE(misindexed, 0);
        // The published order is the definitive one: folders first, natural order, identity index correct.
        QCOMPARE(model.kindAt(0), QString("folder"));
        for (int row : {0, 1, 499, 500, 50000, kHundredThousand - 1}) QCOMPARE(model.rowOf(model.pathAt(row)), row);
    }
    // A newer folder replaces an older one mid-arrival; nothing from the older folder is published afterwards.
    void newerFolderOpenedDuringArrivalWins() {
        QVERIFY(ensure(hundred, hundredReady, kHundredThousand));
        QTemporaryDir little; QVERIFY(generate(little.path(), 50));
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(hundred.path());
        QTRY_VERIFY_WITH_TIMEOUT(model.count() > 0, 60000);
        const bool arrivalCaught = model.arriving();
        model.setDirectory(little.path());
        QCOMPARE(model.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 50, 30000);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        for (int row = 0; row < model.count(); ++row) QVERIFY(model.pathAt(row).startsWith(QDir::cleanPath(little.path())));
        QVERIFY2(arrivalCaught, "the listing finished before the first rows, so no arrival was caught");
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(model.count(), 50);
    }
    // Closing a window mid-arrival neither blocks the GUI thread nor touches the destroyed model.
    void closingDuringArrivalIsBounded() {
        QVERIFY(ensure(hundred, hundredReady, kHundredThousand));
        auto model = std::make_unique<DirectoryEntryModel>(); model->setActive(true);
        model->setDirectory(hundred.path());
        QTRY_VERIFY_WITH_TIMEOUT(model->count() > 0, 60000);
        const bool arrivalCaught = model->arriving();
        QElapsedTimer clock; clock.start();
        model.reset();
        const auto closeMs = clock.elapsed();
        qInfo() << "model destruction during arrival" << closeMs << "ms; arrival caught" << arrivalCaught;
        QVERIFY2(closeMs < 900, "destruction waited on the listing worker");
        QVERIFY(arrivalCaught);
    }

    // The fast path a user mostly sees: nothing provisional, one publication, no reorder.
    void fastListingPublishesOnceWithNoProvisionalRows() {
        QTemporaryDir dir; QVERIFY(generate(dir.path(), 400));
        directory_scan::testSeam = {}; // shipped tuning
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted), layouts(&model, &QAbstractItemModel::layoutChanged), resets(&model, &QAbstractItemModel::modelReset);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 400, 10000);
        QCOMPARE(inserted.count(), 0); QCOMPARE(layouts.count(), 0); QCOMPARE(resets.count(), 1);
    }
    void arrivalSortsEachBatchAndPublishesTheDefinitiveOrderOnce() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 500; folder.heldAfter = 256;
        folder.name = [](int i) { return QString("f%1.png").arg(1000 - i); }; // arrives backwards, so the order must change
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged), resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.setDirectory(dir.path());
        const int resetsAtOpen = resets.count();
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 256, 10000);
        QVERIFY(model.loading()); QVERIFY(model.arriving());
        QCOMPARE(resets.count(), resetsAtOpen); QCOMPARE(layouts.count(), 0);
        QVERIFY2(inserted.count() < 16, "more than one model signal per few batches");
        // First batch sorted among itself: 745..808, then the next, older-looking batch appended after it.
        QCOMPARE(model.pathAt(0).section('/', -1), QString("f937.png"));
        // A cursor and persistent index set on a provisional row follow its identity through the definitive order.
        const int chosenRow = model.rowOf(dir.filePath("f900.png"));
        QVERIFY(chosenRow >= 0);
        model.select(chosenRow);
        const QPersistentModelIndex persistent(model.index(chosenRow));
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 500, 10000);
        QCOMPARE(layouts.count(), 1); QCOMPARE(resets.count(), resetsAtOpen);
        QStringList expected; for (int i = 501; i <= 1000; ++i) expected << QString("f%1.png").arg(i);
        QCOMPARE(names(model), expected);
        const int finalRow = model.rowOf(dir.filePath("f900.png"));
        QCOMPARE(model.pathAt(finalRow), dir.filePath("f900.png"));
        QCOMPARE(persistent.row(), finalRow); QCOMPARE(model.cursorRow(), finalRow);
        QCOMPARE(model.selectedPaths(), QStringList{dir.filePath("f900.png")});
    }
    // The order and rows land before the identity probes; `loading` keeps its meaning until the baseline does.
    void definitiveOrderDoesNotWaitForIdentityProbes() {
        QTemporaryDir dir; QVERIFY(generate(dir.path(), 3000));
        directory_scan::testSeam = {}; // shipped tuning, real files, real probes
        DirectoryEntryModel model; model.setActive(true);
        bool definitiveWhileLoading = false; int rowsAtDefinitive = -1;
        connect(&model, &DirectoryEntryModel::arrivingChanged, this, [&] {
            if (model.arriving()) return;
            definitiveWhileLoading = model.loading(); rowsAtDefinitive = model.count();
        });
        model.setDirectory(dir.path());
        QVERIFY(model.arriving());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 30000);
        QVERIFY(!model.arriving());
        QCOMPARE(rowsAtDefinitive, 3000);
        QVERIFY2(definitiveWhileLoading, "the definitive order waited for the identity baseline");
        // The baseline landed with `loading`: a rename right after it keeps the selection, as it always has.
        const QString before = dir.filePath("img17.png"), after = dir.filePath("renamed.png");
        model.select(model.rowOf(before));
        const bool renameKnown = QFileInfo(before).birthTime().isValid();
        QVERIFY(QFile::rename(before, after));
        model.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.rowOf(after) >= 0, 30000);
        if (renameKnown) QCOMPARE(model.selectedPaths(), QStringList{after});
    }
    // The window between the rows landing and their identities: a file renamed on disk, or deleted by the user, there.
    void renameOnDiskDuringTheProbeWindowKeepsPathsRight() {
        QTemporaryDir dir; QVERIFY(generate(dir.path(), 100));
        ProbeHold hold; SeamScope seam(nullptr); hold.install(); directory_scan::testSeam.tuning = {};
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(hold.entered->load() && model.count() == 100 && !model.arriving(), 10000);
        QVERIFY(model.loading());
        const QString before = dir.filePath("img1.png"), after = dir.filePath("moved.png"), neighbour = dir.filePath("img2.png");
        model.select(model.rowOf(before));
        model.select(model.rowOf(neighbour), DirectoryEntryModel::Toggle);
        // Selection is by path, so what a copy or delete would act on is exact while identities are unknown.
        QCOMPARE(model.selectedCount(), 2); QVERIFY(model.selectedPaths().contains(before));
        QVERIFY(QFile::rename(before, after));
        model.refresh(); // the watcher's notification
        hold.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.rowOf(after) >= 0 && model.rowOf(before) < 0, 30000);
        QCOMPARE(model.count(), 100);
        QCOMPARE(model.pathAt(model.rowOf(after)), after);
        // Membership changed during the probes, so that snapshot proves nothing: the row goes rather than guesses.
        QCOMPARE(model.selectedPaths(), QStringList{neighbour});
    }
    void deleteDuringTheProbeWindowDropsTheRow() {
        QTemporaryDir dir; QVERIFY(generate(dir.path(), 100));
        ProbeHold hold; SeamScope seam(nullptr); hold.install(); directory_scan::testSeam.tuning = {};
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(hold.entered->load() && model.count() == 100, 10000);
        const QString victim = dir.filePath("img3.png");
        model.select(model.rowOf(victim));
        for (const auto& path : model.selectedPaths()) QVERIFY(QFile::remove(path)); // acts through the row's path
        model.refresh();
        hold.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 99 && model.rowOf(victim) < 0, 30000);
        QCOMPARE(model.selectedCount(), 0); QCOMPARE(model.cursorRow(), -1);
        QVERIFY(QFile::exists(dir.filePath("img2.png")));
    }
    // A probe from an older folder is cancelled by opening a newer one, and its baseline never lands.
    void staleProbesCannotChangeANewerListing() {
        QTemporaryDir older, newer; QVERIFY(generate(older.path(), 100)); QVERIFY(generate(newer.path(), 40));
        ProbeHold hold; SeamScope seam(nullptr); hold.install(); directory_scan::testSeam.tuning = {};
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(older.path());
        QTRY_VERIFY_WITH_TIMEOUT(hold.entered->load() && model.count() == 100, 10000);
        model.setDirectory(newer.path());
        QTRY_VERIFY_WITH_TIMEOUT(hold.cancelled->load(), 10000); // the older worker saw its stop token
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 40, 10000);
        QSignalSpy data(&model, &QAbstractItemModel::dataChanged), selection(&model, &DirectoryEntryModel::selectionChanged);
        QTest::qWait(64); // sleep-ok: several drain ticks in which a stale result would publish
        QCOMPARE(data.count(), 0); QCOMPARE(selection.count(), 0); QCOMPARE(model.count(), 40);
        // The newer folder's own baseline is the one in force: a rename there is followed.
        const QString before = newer.filePath("img1.png"), after = newer.filePath("moved.png");
        const bool renameKnown = QFileInfo(before).birthTime().isValid();
        model.select(model.rowOf(before));
        QVERIFY(QFile::rename(before, after));
        model.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.rowOf(after) >= 0, 10000);
        if (renameKnown) QCOMPARE(model.selectedPaths(), QStringList{after});
    }
    // `arriving` never sticks: every way a first listing can end clears it.
    void arrivingAlwaysClears() {
        QTemporaryDir dir;
        { // success, nothing to show
            directory_scan::testSeam = {};
            DirectoryEntryModel model; model.setActive(true);
            model.setDirectory(dir.path()); QVERIFY(model.arriving());
            QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && !model.loading(), 10000);
            QCOMPARE(model.count(), 0); QVERIFY(model.accessible());
        }
        { // denied or missing
            DirectoryEntryModel model; model.setActive(true);
            model.setDirectory(dir.filePath("missing")); QVERIFY(model.arriving());
            QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && !model.loading(), 10000);
            QVERIFY(!model.accessible());
        }
        { // failing partway
            FakeFolder folder; folder.total = 500; folder.failAfter = 300;
            SeamScope seam(&folder);
            DirectoryEntryModel model; model.setActive(true);
            model.setDirectory(dir.path()); QVERIFY(model.arriving());
            QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && !model.loading(), 10000);
            QVERIFY(!model.accessible());
        }
        const auto heldModel = [&](DirectoryEntryModel& model) {
            model.setActive(true); model.setDirectory(dir.path());
            return QTest::qWaitFor([&] { return model.count() >= 64; }, 10000) && model.arriving();
        };
        { // cancelled by going inactive, and by no folder at all
            FakeFolder folder; folder.total = 500; folder.heldAfter = 64; SeamScope seam(&folder);
            DirectoryEntryModel model; QVERIFY(heldModel(model));
            model.setActive(false); QVERIFY(!model.arriving()); QVERIFY(!model.loading());
            model.setActive(true); QVERIFY(model.arriving());
            model.setDirectory({}); QVERIFY(!model.arriving()); QVERIFY(!model.loading());
        }
        { // a filter changing during arrival neither clears it early nor leaves it set
            FakeFolder folder; folder.total = 500; folder.heldAfter = 64; SeamScope seam(&folder);
            DirectoryEntryModel model; QVERIFY(heldModel(model));
            model.setFilter("f00", true); QVERIFY(model.arriving());
            model.clearFilter(); QVERIFY(model.arriving()); QCOMPARE(model.count(), 64);
            model.setKindFilter("folder"); QVERIFY(model.arriving()); QCOMPARE(model.count(), 0);
            folder.release();
            QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && !model.loading(), 10000);
            model.setKindFilter({}); QCOMPARE(model.count(), 500);
        }
        { // a filter hiding everything still ends
            FakeFolder folder; folder.total = 500; SeamScope seam(&folder);
            DirectoryEntryModel model; model.setActive(true); model.setFilter("nothing matches this", true);
            model.setDirectory(dir.path());
            QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && !model.loading(), 10000);
            QCOMPARE(model.count(), 0);
        }
        { // destroyed while set: nothing to clear, nothing to crash
            FakeFolder folder; folder.total = 500; folder.heldAfter = 64; SeamScope seam(&folder);
            DirectoryEntryModel model; QVERIFY(heldModel(model));
        }
    }
    void sortedArrivalPublishesNoSecondOrder() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 500;
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged), inserted(&model, &QAbstractItemModel::rowsInserted);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 500, 10000);
        QVERIFY(inserted.count() > 0);
        QCOMPARE(layouts.count(), 0);
    }
    // Folders publish above files as they arrive, without rewriting the index, and rowOf stays exact throughout.
    void foldersArriveAboveFilesAndTheIndexStaysExact() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 2000;
        folder.name = [](int i) { return i % 10 == 0 ? QString("dir%1").arg(i, 5, 10, QChar('0')) : QString("f%1.png").arg(i, 5, 10, QChar('0')); };
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        int mismatches = 0, foldersBelowFiles = 0, ticks = 0;
        connect(&model, &DirectoryEntryModel::countChanged, this, [&] {
            if (!model.arriving()) return;
            ++ticks; bool sawFile = false;
            for (int row = 0; row < model.count(); ++row) {
                if (model.rowOf(model.pathAt(row)) != row) ++mismatches;
                if (model.kindAt(row) == "folder") { if (sawFile) ++foldersBelowFiles; } else sawFile = true;
            }
        });
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        QVERIFY(ticks > 0); QCOMPARE(mismatches, 0); QCOMPARE(foldersBelowFiles, 0);
        for (int row = 0; row < 2000; ++row) QCOMPARE(model.rowOf(model.pathAt(row)), row);
    }
    // A worker never queues without bound: with the GUI busy it stops reading, and the stop token still frees it.
    void aBusyGuiStopsTheWorkerAtTheQueueBound() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 200000;
        SeamScope seam(&folder);
        directory_scan::testSeam.tuning.queuedRows = 1024; directory_scan::testSeam.tuning.batchRows = 256;
        auto exits = folder.exits, emitted = folder.emitted;
        auto model = std::make_unique<DirectoryEntryModel>(); model->setActive(true);
        model->setDirectory(dir.path());
        // No event processing here, so nothing drains. The bound is the queue, one unflushed batch and the one in hand.
        QElapsedTimer deadline; deadline.start();
        while (emitted->load() < 1024 && deadline.elapsed() < 10000) std::this_thread::yield();
        QTest::qSleep(200); // sleep-ok: asserting the worker has stopped reading
        const int reached = emitted->load();
        qInfo() << "worker stopped after" << reached << "entries with a 1024-row queue";
        QVERIFY(reached >= 1024);
        QVERIFY2(reached <= 1024 + 2 * 256, qPrintable(QString("worker read %1 entries past the queue bound").arg(reached)));
        QElapsedTimer clock; clock.start();
        model.reset();
        const auto closeMs = clock.elapsed();
        QVERIFY2(closeMs < 900, "closing waited out the pool timeout instead of waking the blocked worker");
        QTRY_COMPARE_WITH_TIMEOUT(exits->load(), 1, 10000);
    }
    // Stale rows: the older folder's queued and in-flight rows never reach the newer folder.
    void aHeldOlderFolderCannotPublishIntoANewerOne() {
        QTemporaryDir older, newer; QVERIFY(generate(newer.path(), 40));
        FakeFolder held; held.total = 5000; held.heldAfter = 128;
        const auto heldSource = held.enumerator();
        const auto olderKey = directory_scan::keyFor(older.path());
        SeamScope seam(nullptr);
        directory_scan::testSeam.enumerator = [=](const std::filesystem::path& directory, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            if (directory_scan::keyFor(directory_scan::displayPath(directory)) == directory_scan::keyFor(directory_scan::displayPath(std::filesystem::canonical(directory_scan::nativePath(olderKey)))))
                return heldSource(directory, hidden, cancel, limit, sink);
            return directory_scan::enumerate(directory, hidden, cancel, limit, sink);
        };
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.setDirectory(older.path());
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 128, 10000);
        model.setDirectory(newer.path());
        QCOMPARE(model.count(), 0); // released synchronously with the folder change
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 40, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(held.exits->load(), 1, 10000);
        const auto settled = inserted.count();
        held.release();
        QTest::qWait(64); // sleep-ok: several drain ticks in which a stale batch would publish
        QCOMPARE(inserted.count(), settled);
        QCOMPARE(model.count(), 40);
        for (int row = 0; row < model.count(); ++row) QVERIFY(model.pathAt(row).startsWith(QDir::cleanPath(newer.path())));
    }
    // A folder that vanishes or is denied mid-listing: no partial rows survive as if they were the folder.
    void failureMidListingClearsRowsAndKeepsSelectionForRecovery() {
        QTemporaryDir dir;
        FakeFolder failing; failing.total = 500; failing.failAfter = 300;
        {
            SeamScope seam(&failing);
            DirectoryEntryModel model; model.setActive(true);
            QSignalSpy failed(&model, &DirectoryEntryModel::listingFailed);
            bool partialSeen = false;
            int selectedRow = -1;
            connect(&model, &DirectoryEntryModel::countChanged, this, [&] {
                if (model.count() > 0 && model.loading()) {
                    partialSeen = true;
                    if (selectedRow < 0) { selectedRow = 0; model.select(0); }
                }
            });
            model.setDirectory(dir.path());
            QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
            QVERIFY(partialSeen);
            QCOMPARE(model.count(), 0); QVERIFY(!model.accessible());
            QCOMPARE(failed.count(), 1); QVERIFY(!failed.first().first().toString().isEmpty());
            QCOMPARE(model.selectedCount(), 0); // nothing actionable while failed
            QCOMPARE(model.rowOf(dir.filePath("f0000000.png")), -1);
        }
        // The same model, recovering: the source heals on the next listing, and the kept selection returns.
        auto attempts = std::make_shared<std::atomic_int>(0);
        FakeFolder healthy; healthy.total = 500;
        const auto failingSource = failing.enumerator(), healthySource = healthy.enumerator();
        SeamScope seam(nullptr);
        directory_scan::testSeam.enumerator = [=](const std::filesystem::path& directory, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return attempts->fetch_add(1) == 0 ? failingSource(directory, hidden, cancel, limit, sink) : healthySource(directory, hidden, cancel, limit, sink);
        };
        DirectoryEntryModel model; model.setActive(true);
        bool selected = false;
        connect(&model, &DirectoryEntryModel::countChanged, this, [&] { if (!selected && model.count() > 0) { selected = true; model.select(0); } });
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && !model.accessible(), 10000);
        QVERIFY(selected);
        model.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.accessible() && model.count() == 500, 10000);
        QCOMPARE(model.selectedCount(), 1);
    }
    // A change notification mid-listing lets the listing finish, then refreshes; restarting would starve a busy folder.
    void refreshDuringArrivalFinishesThenRefreshes() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 500; folder.heldAfter = 256;
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.setDirectory(dir.path());
        const int atOpen = resets.count();
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 256, 10000);
        model.refresh(); model.refresh();
        QCOMPARE(folder.starts->load(), 1);
        QCOMPARE(model.count(), 256); // rows stay
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 500 && folder.starts->load() == 2, 10000);
        QCOMPARE(resets.count(), atOpen); // the follow-up refresh found the same rows in the same order
    }
    // A new sort mid-arrival abandons the provisional rows: the new order arrives, never a mix of the two.
    void sortChangeDuringArrivalRestarts() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 500; folder.heldAfter = 256;
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 256, 10000);
        model.setSort("name", true);
        QCOMPARE(model.count(), 0);
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 500, 10000);
        QCOMPARE(folder.starts->load(), 2);
        QCOMPARE(model.pathAt(0).section('/', -1), QString("f0000499.png"));
        QCOMPARE(model.pathAt(499).section('/', -1), QString("f0000000.png"));
    }
    // A filter applies to arriving rows, can change mid-arrival, and sees every physical row in the end.
    void filterDuringArrivalProjectsEachBatch() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 1000; folder.heldAfter = 512;
        folder.name = [](int i) { return QString("%1%2.png").arg(i % 2 ? "odd" : "even").arg(i, 4, 10, QChar('0')); };
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        model.setFilter("odd", true);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.setDirectory(dir.path());
        const int atOpen = resets.count();
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 256, 10000);
        QCOMPARE(resets.count(), atOpen); // no reset per batch
        QVERIFY(names(model).first().startsWith("odd"));
        QCOMPARE(model.rowOf(dir.filePath("even0000.png")), -1);
        model.setFilter("even", true);
        QCOMPARE(model.count(), 256);
        QCOMPARE(model.rowOf(model.pathAt(100)), 100);
        QVERIFY(names(model).last().startsWith("even"));
        model.clearFilter();
        QCOMPARE(model.count(), 512);
        QCOMPARE(model.rowOf(model.pathAt(300)), 300);
        model.setFilter("odd", true);
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 500, 10000);
        for (int row = 0; row < 500; ++row) QCOMPARE(model.rowOf(model.pathAt(row)), row);
        model.clearFilter();
        QCOMPARE(model.count(), 1000);
    }
    // PERF-ENUM parity: the streaming enumerator and the frozen listDirectory classify the same folder identically.
    void enumeratorMatchesListDirectory() {
        QTemporaryDir dir;
        QVERIFY(generate(dir.path(), 300));
        QVERIFY(QFile(dir.filePath(".dot.png")).open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        QVERIFY(QFile(dir.filePath("attr.png")).open(QIODevice::WriteOnly));
        QVERIFY(SetFileAttributesW(directory_scan::nativePath(dir.filePath("attr.png")).c_str(), FILE_ATTRIBUTE_HIDDEN));
#endif
        const auto root = directory_scan::nativePath(dir.path());
        std::error_code linkError;
        std::filesystem::create_directory_symlink(root / "folder0", root / "dirlink", linkError);
        if (linkError) qInfo() << "directory symlink unavailable:" << QString::fromStdString(linkError.message());
        std::filesystem::create_symlink(root / "img1.png", root / "filelink.png", linkError);
        if (linkError) qInfo() << "file symlink unavailable:" << QString::fromStdString(linkError.message());
        const auto signature = [](const glancelot::DirectoryEntry& e) {
            return QString("%1|%2|%3|%4|%5|%6|%7").arg(directory_scan::displayPath(e.path)).arg(int(e.kind)).arg(e.link).arg(e.hidden)
                .arg(e.meta.known).arg(e.meta.modified).arg(e.meta.size);
        };
        for (const bool hidden : {false, true}) {
            auto reference = glancelot::listDirectory(root, {}, hidden);
            QVERIFY(reference.accessible); QVERIFY(!reference.truncated);
            QStringList expected, actual;
            for (const auto& entry : reference.entries) expected << signature(entry);
            const auto outcome = directory_scan::enumerate(root, hidden, {}, 100000, [&](glancelot::DirectoryEntry&& entry) { actual << signature(entry); return true; });
            QVERIFY(outcome.accessible); QVERIFY(!outcome.truncated); QVERIFY(!outcome.cancelled);
            expected.sort(); actual.sort();
            QCOMPARE(actual, expected);
        }
        // Limit: both stop at it and say so. Exactly at the limit is not truncated.
        const auto full = glancelot::listDirectory(root, {}, true).entries.size();
        for (const std::size_t limit : {std::size_t(10), full - 1, full}) {
            const auto reference = glancelot::listDirectory(root, {}, true, {}, limit);
            std::size_t received = 0;
            const auto outcome = directory_scan::enumerate(root, true, {}, limit, [&](glancelot::DirectoryEntry&&) { ++received; return true; });
            QCOMPARE(received, reference.entries.size()); QCOMPARE(outcome.truncated, reference.truncated);
        }
        // Failure and cancellation.
        QVERIFY(!directory_scan::enumerate(root / "missing", true, {}, 10, [](glancelot::DirectoryEntry&&) { return true; }).accessible);
        std::stop_source stopped; stopped.request_stop();
        QVERIFY(directory_scan::enumerate(root, true, stopped.get_token(), 10, [](glancelot::DirectoryEntry&&) { return true; }).cancelled);
    }
    // PERF-ENUM parity: precomputed keys order exactly as glancelot's order, on hostile generated names.
    void sortKeysMatchTheViewersOrder() {
        std::mt19937 random(20261104);
        const QStringList pieces{"a", "B", "c", "Z", "z", "0", "00", "007", "1", "9", "10", "99999999999999999999", "-", "_", " ", ".", "/x", ":", "é", "É", "中", "~", "#"};
        std::vector<glancelot::DirectoryEntry> entries;
        std::vector<directory_scan::NameKey> keys;
        const auto root = std::filesystem::path("generated");
        QSet<QString> seen;
        while (entries.size() < 1500) {
            QString name;
            const int parts = 1 + int(random() % 6);
            for (int i = 0; i < parts; ++i) name += pieces[int(random() % pieces.size())];
            name.replace('/', '-');
            if (seen.contains(name)) continue;
            seen.insert(name);
            glancelot::DirectoryEntry entry;
            entry.path = root / directory_scan::nativePath(name);
            entry.meta = {std::int64_t(random() % 5), std::uintmax_t(random() % 5), random() % 7 != 0};
            keys.push_back(directory_scan::nameKey(entry.path.filename().native()));
            entries.push_back(std::move(entry));
        }
        using glancelot::SortKey;
        for (const auto key : {SortKey::Name, SortKey::Modified, SortKey::Size, SortKey::Shuffle}) {
            for (const bool descending : {false, true}) {
                const glancelot::SortSpec spec{key, descending, 42};
                const directory_scan::ScanOrder order(entries, keys, spec, {});
                std::vector<std::uint32_t> fast(entries.size());
                std::iota(fast.begin(), fast.end(), 0U);
                std::vector<std::uint32_t> reference = fast;
                order.sort(fast);
                std::sort(reference.begin(), reference.end(), [&](std::uint32_t a, std::uint32_t b) {
                    return glancelot::fileSortBefore(spec, entries[a].path, entries[a].meta, entries[b].path, entries[b].meta);
                });
                QVERIFY2(fast == reference, qPrintable(QString("order differs for key %1 descending %2").arg(int(key)).arg(descending)));
            }
        }
    }
    // Continuation beyond the first cap (F016): loadMore().
    void shippedContinuationSteps() {
        QCOMPARE(directory_scan::Tuning{}.entryStep, std::size_t(100000));
        QCOMPARE(directory_scan::Tuning{}.entryLimit, std::size_t(1000000));
    }
    void loadMoreListsTheNextPageAndPublishesOnce() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore());
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset), inserted(&model, &QAbstractItemModel::rowsInserted), layouts(&model, &QAbstractItemModel::layoutChanged);
        model.loadMore();
        QVERIFY(model.loading()); QVERIFY(model.canLoadMore());
        QCOMPARE(model.count(), kPage); // the rows stay until the new listing is finished
        QVERIFY(listed(model, 2000));
        QCOMPARE(resets.count(), 1); QCOMPARE(inserted.count(), 0); QCOMPARE(layouts.count(), 0);
        const QStringList shown = names(model);
        QVERIFY(std::is_sorted(shown.cbegin(), shown.cend()));
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore());
        QSignalSpy changed(&model, &DirectoryEntryModel::directoryChanged);
        model.loadMore();
        QVERIFY(listed(model, 2500));
        QVERIFY(!model.truncated()); QVERIFY(!model.canLoadMore());
        QVERIFY(changed.count() > 0);
    }
    void aSecondPressWhileListingDoesNothing() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.folder.heldAfter = 1500; paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), 2, 10000);
        model.loadMore(); model.refresh(); // the refresh waits for the listing it would otherwise restart
        QCOMPARE(paged.folder.starts->load(), 2);
        QTest::qWait(64); // sleep-ok: several ticks in which a second scan would start
        QCOMPARE(paged.folder.starts->load(), 2);
        QCOMPARE(model.count(), kPage);
        paged.folder.release();
        QVERIFY(listed(model, 2000)); // one step, not two
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), 3, 10000); // the coalesced refresh
        QVERIFY(listed(model, 2000));
    }
    void aNewerFolderCancelsLoadMoreAndStartsAtTheFirstCap() {
        QTemporaryDir older, newer; PagedFolder paged(5000); paged.folder.heldAfter = 1500; paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(older.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), 2, 10000);
        model.setDirectory(newer.path());
        QTRY_VERIFY_WITH_TIMEOUT(paged.folder.exits->load() >= 2, 10000); // the held worker saw its stop token
        QVERIFY(listed(model, kPage)); // the first cap: it stops before the hold
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.exits->load(), paged.folder.starts->load(), 10000);
        QVERIFY(model.pathAt(0).startsWith(QDir::fromNativeSeparators(newer.path())));
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore());
    }
    void selectionCursorAndAnchorSurviveLoadMore() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        const QString picked = model.pathAt(10), other = model.pathAt(20);
        model.select(10);
        model.select(20, DirectoryEntryModel::Toggle);
        model.select(10); model.select(20, DirectoryEntryModel::Toggle);
        const int before = model.rowOf(other);
        model.loadMore();
        QVERIFY(listed(model, 2000));
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(model.selectedPaths().contains(picked)); QVERIFY(model.selectedPaths().contains(other));
        QVERIFY(model.rowOf(other) != before); // the page landed above them, so this proves the selection moved with its rows
        QCOMPARE(model.cursorRow(), model.rowOf(other));
        // The anchor too: a range from it reaches the row it should.
        model.select(model.rowOf(other) + 3, DirectoryEntryModel::Range);
        QCOMPARE(model.selectedCount(), 4);
    }
    void theHardLimitStopsOffering() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, 2000));
        QSignalSpy changed(&model, &DirectoryEntryModel::directoryChanged);
        model.loadMore();
        QVERIFY(model.canLoadMore()); // the banner keeps its message until this listing lands
        QVERIFY(listed(model, kPagedLimit));
        QVERIFY(changed.count() > 0); // QML reads canLoadMore through this signal
        QVERIFY(model.truncated()); QVERIFY(!model.canLoadMore());
        const int starts = paged.folder.starts->load();
        model.loadMore();
        QVERIFY(!model.loading()); QCOMPARE(paged.folder.starts->load(), starts);
    }
    void anOrdinaryRefreshKeepsATruncatedFoldersRows() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, 2000));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        const int starts = paged.folder.starts->load();
        model.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), starts + 1, 10000);
        QVERIFY(listed(model, 2000)); // at the raised cap
        QCOMPARE(resets.count(), 0);
    }
    void failureDuringLoadMoreEndsTheOffer() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.folder.failAfter = 1500; paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, 0));
        QVERIFY(!model.accessible()); QVERIFY(!model.truncated()); QVERIFY(!model.canLoadMore());
    }
    void filteredLoadMoreFindsMatchesFurtherOn() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setFilter("f0000500", true); // i == 2000: past the first two pages
        model.setDirectory(dir.path());
        QVERIFY(listed(model, 0));
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore());
        model.loadMore();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QCOMPARE(model.count(), 0);
        model.loadMore();
        QVERIFY(listed(model, 1));
        QVERIFY(!model.truncated());
        model.clearFilter();
        QCOMPARE(model.count(), 2500);
    }
    void goingInactiveDuringLoadMoreKeepsTheRaisedCap() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.folder.heldAfter = 1500; paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), 2, 10000);
        model.setActive(false);
        QVERIFY(!model.loading());
        model.loadMore(); QVERIFY(!model.loading()); // nothing to list for
        paged.folder.release();
        model.setActive(true);
        QVERIFY(listed(model, 2000));
    }
    // The last page's listing is the only thing keeping the offer open: abandoning it must say the offer has ended.
    void abandoningTheLastPageNotifiesThatTheOfferEnded() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.folder.heldAfter = 2500; paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, 2000));
        model.loadMore();
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), 3, 10000);
        QVERIFY(model.canLoadMore());
        QSignalSpy changed(&model, &DirectoryEntryModel::directoryChanged);
        model.setActive(false);
        QVERIFY(!model.canLoadMore());
        QCOMPARE(changed.count(), 1);
    }
    void loadMoreStaysInsideTheGuiBudget() {
        QTemporaryDir dir; FakeFolder folder; folder.total = 250000;
        SeamScope seam(&folder, 2048);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == kHundredThousand, 60000);
        Tracer tracer(model, 200000);
        tracer.begin();
        model.loadMore();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 200000, 60000);
        tracer.finish();
        qInfo().noquote() << describe("continuation-200k", tracer.trace);
        QCOMPARE(tracer.trace.resets, 1);
        QVERIFY2(tracer.trace.worstGapMs < kGuiGapBudgetMs, qPrintable(QString("the GUI stalled for %1 ms").arg(tracer.trace.worstGapMs)));
    }
    // Not a default test: a million entries cost about a gigabyte. Set GLANCELOT_PERF_ENUM_MILLION to "real" (generated
    // empty files on this disk) or "synthetic" (the seam) and record the output in the handoff.
    void traceOneMillion() {
        const QByteArray mode = qgetenv("GLANCELOT_PERF_ENUM_MILLION");
        if (mode.isEmpty()) QSKIP("set GLANCELOT_PERF_ENUM_MILLION to real or synthetic");
        const int total = 1000100;
        QTemporaryDir dir; FakeFolder folder; folder.total = total;
        QElapsedTimer setup; setup.start();
        if (mode == "real") QVERIFY(generate(dir.path(), total));
        qInfo() << "generated in" << setup.elapsed() << "ms";
        std::unique_ptr<SeamScope> seam;
        if (mode != "real") seam = std::make_unique<SeamScope>(&folder, 2048);
        else directory_scan::testSeam = {};
        const Memory empty = memoryNow();
        qInfo().noquote() << QString("PERF_ENUM_MEMORY %1 before rows=0 working_set_mb=%2 peak_mb=%3").arg(mode).arg(empty.workingSetMb).arg(empty.peakMb);
        {
            DirectoryEntryModel model; model.setActive(true);
            const auto step = [&](const char* name, auto&& start) {
                Tracer tracer(model, model.count() + 100000);
                tracer.begin();
                start();
                const bool done = QTest::qWaitFor([&] { return !model.loading(); }, 280000);
                tracer.finish();
                const Memory now = memoryNow();
                qInfo().noquote() << describe(name, tracer.trace);
                qInfo().noquote() << QString("PERF_ENUM_MEMORY %1 %2 rows=%3 settled_ms=%4 working_set_mb=%5 peak_mb=%6")
                    .arg(mode).arg(name).arg(model.count()).arg(tracer.trace.settledMs).arg(now.workingSetMb).arg(now.peakMb);
                return done;
            };
            QVERIFY(step("first-page", [&] { model.setDirectory(dir.path()); }));
            QCOMPARE(model.count(), kHundredThousand);
            while (model.canLoadMore()) QVERIFY(step("show-more", [&] { model.loadMore(); }));
            QCOMPARE(model.count(), 1000000);
            QVERIFY(model.truncated());
        }
        directory_scan::testSeam = {};
    }
    void arrivalsQueueBlocksAtCapacityAndWakesOnStop() {
        directory_scan::Arrivals queue(100);
        std::atomic_int pushed{}; std::atomic_bool refused{};
        std::stop_source stop;
        std::thread producer([&] {
            for (int i = 0; i < 4; ++i) {
                directory_scan::Batch batch; batch.files.resize(50);
                if (!queue.push(std::move(batch), stop.get_token())) { refused = true; return; }
                ++pushed;
            }
        });
        QTRY_COMPARE_WITH_TIMEOUT(pushed.load(), 2, 10000);
        QTest::qSleep(50); // sleep-ok: asserting the producer is waiting, not pushing
        QCOMPARE(pushed.load(), 2);
        QVERIFY(queue.take().has_value());
        QTRY_COMPARE_WITH_TIMEOUT(pushed.load(), 3, 10000);
        stop.request_stop();
        producer.join();
        QVERIFY(refused.load());
        QCOMPARE(pushed.load(), 3);
        QVERIFY(queue.take().has_value()); QVERIFY(queue.take().has_value()); QVERIFY(!queue.take().has_value());
    }
};
QTEST_GUILESS_MAIN(PerfEnumTests)
#include "perf_enum_tests.moc"
