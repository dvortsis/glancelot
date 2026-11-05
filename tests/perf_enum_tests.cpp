#include "glancelot/directory_entry_model.hpp"
#include "glancelot/perf_trace.hpp"
#include "perf_enum_test_support.hpp"
#include <QDeadlineTimer>
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
#include <chrono>
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
// A paged folder whose contents change when asked, or that hold hidden files the plain one does not list.
struct SwitchingFolder : PagedFolder {
    FakeFolder other;
    std::shared_ptr<std::atomic_bool> useOther = std::make_shared<std::atomic_bool>(false);
    explicit SwitchingFolder(int total) : PagedFolder(total) {
        other.total = total + 1;
        other.name = [plain = folder.name](int i) { return i == 0 ? QString("a-other.png") : plain(i - 1); };
    }
    void installSwitchedBy(bool byHidden) {
        install();
        directory_scan::testSeam.enumerator = [plain = folder.enumerator(), changed = other.enumerator(), use = useOther, byHidden](
            const std::filesystem::path& directory, bool includeHidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return ((byHidden ? includeHidden : use->load()) ? changed : plain)(directory, includeHidden, cancel, limit, sink);
        };
    }
};
// Rows rebuilt the way a scan builds them, spread across the model, to report what the estimate sees.
std::vector<directory_scan::Row> sampledRows(const DirectoryEntryModel& model) {
    std::vector<directory_scan::Row> rows;
    const int step = std::max(1, model.count() / 1024);
    for (int i = 0; i < model.count(); i += step) {
        directory_scan::Row row;
        row.path = model.pathAt(i); row.name = model.data(model.index(i), DirectoryEntryModel::NameRole).toString(); row.key = directory_scan::keyFor(row.path);
        rows.push_back(std::move(row));
    }
    return rows;
}
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
        // The last publish ends the wait, so the heartbeat never fires after it: count that stretch too.
        trace.worstGapMs = std::max(trace.worstGapMs, trace.settledMs - previous);
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

// Lists a folder whole, leaves the cursor on a row that arrives late, then lists it again up to that point.
struct CursorStillToArrive {
    QTemporaryDir dir;
    FakeFolder whole, held;
    std::shared_ptr<std::atomic_bool> hold = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<SeamScope> seam;
    std::unique_ptr<DirectoryEntryModel> model;
    QString path;
    bool start(int folderEvery) {
        const auto names = [folderEvery](int i) {
            return folderEvery && i % folderEvery == 0 ? QString("dir%1").arg(2000 - i, 5, 10, QChar('0')) : QString("f%1.png").arg(2000 - i, 5, 10, QChar('0'));
        };
        whole.total = held.total = 2000; whole.name = held.name = names; held.heldAfter = 1024;
        seam = std::make_unique<SeamScope>(nullptr);
        directory_scan::testSeam.enumerator = [a = whole.enumerator(), b = held.enumerator(), use = hold](
            const std::filesystem::path& d, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return (use->load() ? b : a)(d, hidden, cancel, limit, sink);
        };
        model = std::make_unique<DirectoryEntryModel>();
        model->setActive(true);
        model->setDirectory(dir.path());
        if (!QTest::qWaitFor([&] { return !model->loading() && model->count() == 2000; }, 10000)) return false;
        path = QDir(dir.path()).filePath(QString("f%1.png").arg(2000 - 1501, 5, 10, QChar('0'))); // enumerated after the hold
        model->select(model->rowOf(path));
        if (model->pathAt(model->cursorRow()) != path) return false;
        model->setActive(false);
        hold->store(true);
        model->setActive(true);
        return true;
    }
};
// The selection totals, read the slow way: what each row says about itself.
std::pair<int, qint64> totalsFromRows(const DirectoryEntryModel& model) {
    int count = 0; qint64 bytes = 0;
    for (int i = 0; i < model.count(); ++i) {
        const QModelIndex at = model.index(i);
        if (!model.data(at, DirectoryEntryModel::SelectedRole).toBool()) continue;
        ++count;
        const qint64 size = model.data(at, DirectoryEntryModel::SizeRole).toLongLong();
        if (size > 0) bytes += size;
    }
    return {count, bytes};
}
QStringList pathsFromRows(const DirectoryEntryModel& model) {
    QStringList paths;
    for (int i = 0; i < model.count(); ++i) if (model.data(model.index(i), DirectoryEntryModel::SelectedRole).toBool()) paths << model.pathAt(i);
    return paths;
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
        // R1: useful content does not wait for per-file identity probes, which are nearly all of the wait. On base, the
        // first row and the end of loading came together, at about 530 to 920 ms on this machine under one gate lock.
        QVERIFY2(second.firstRowMs < second.settledMs / 2, qPrintable(QString("10k first row took %1 of %2 ms").arg(second.firstRowMs).arg(second.settledMs)));
        QVERIFY2(second.definitiveMs < second.settledMs / 2, qPrintable(QString("10k definitive order took %1 of %2 ms").arg(second.definitiveMs).arg(second.settledMs)));
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
    // Each gate on its own: a listing that is quick, or small, shows no provisional rows; past the floor, any listing does.
    void arrivalGatesHoldBackQuickAndSmallListings() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 500;
        const auto insertsWith = [&](int patienceMs, int minRows, int floorMs) {
            SeamScope seam(&folder);
            directory_scan::testSeam.tuning.patienceMs = patienceMs; directory_scan::testSeam.tuning.minArrivalRows = minRows;
            directory_scan::testSeam.tuning.slowFloorMs = floorMs;
            DirectoryEntryModel model; model.setActive(true);
            QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
            model.setDirectory(dir.path());
            return QTest::qWaitFor([&] { return !model.loading() && model.count() == 500; }, 10000) ? int(inserted.count()) : -1;
        };
        QCOMPARE(insertsWith(600000, 1, 600000), 0);      // within patience
        QCOMPARE(insertsWith(0, 100000, 600000), 0);      // too few rows
        QVERIFY(insertsWith(0, 1, 600000) > 0);           // past both
        QVERIFY(insertsWith(600000, 100000, 0) > 0);      // past the floor, though small and quick
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
            rowsAtDefinitive = model.count();
            // Next turn of the event loop: the end of a whole listing clears `arriving` and `loading` in one call.
            QTimer::singleShot(0, &model, [&] { definitiveWhileLoading = model.loading(); });
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
        FakeFolder folder; folder.total = 2000; folder.heldAfter = 1024; // so a later tick has files above its folders to insert around
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
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1024, 10000);
        const int ticksBefore = ticks;
        QVERIFY(ticksBefore > 0);
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        QVERIFY(ticks > ticksBefore); QCOMPARE(mismatches, 0); QCOMPARE(foldersBelowFiles, 0);
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
    // A listing that finished before the newer folder opened was never cancelled: only its generation stops it.
    void aFinishedOlderListingCannotPublishIntoANewerOne() {
        QTemporaryDir older, newer;
        FakeFolder folder; folder.total = 10;
        SeamScope seam(&folder);
        directory_scan::testSeam.tuning.patienceMs = 600000; directory_scan::testSeam.tuning.slowFloorMs = 600000; // one publish, with rows
        DirectoryEntryModel model; model.setActive(true);
        const QString olderPath = QDir::cleanPath(older.path());
        bool sawOlder = false;
        const auto look = [&] { if (model.count() > 0 && model.pathAt(0).startsWith(olderPath)) sawOlder = true; };
        connect(&model, &QAbstractItemModel::modelReset, this, look);
        connect(&model, &DirectoryEntryModel::countChanged, this, look);
        model.setDirectory(older.path());
        // No event loop here, so the finished listing's signal stays queued until the newer folder has opened.
        QDeadlineTimer deadline(10000);
        while (folder.exits->load() < 1 && !deadline.hasExpired()) std::this_thread::yield();
        QCOMPARE(folder.exits->load(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // sleep-ok: the worker's last steps after the enumerator have nothing to wait on
        model.setDirectory(newer.path());
        QVERIFY(model.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 10, 10000);
        QVERIFY(!sawOlder);
        QVERIFY(model.pathAt(0).startsWith(QDir::cleanPath(newer.path())));
    }
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
    void aHardLimitBetweenStepsIsStillTheLimit() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.install();
        directory_scan::testSeam.tuning.entryLimit = kPage + kPage / 2;
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, kPage + kPage / 2));
        QVERIFY(model.truncated()); QVERIFY(!model.canLoadMore());
    }
    // A page replaces the rows behind an unfiltered view, so a later filter has to project the new rows, and clearing it
    // has to bring back their index.
    void aFilterAfterLoadMoreSeesTheNewPageAndClearingItRestoresTheIndex() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(listed(model, 2000));
        model.setFilter("f0003500", true); // i == 1500: only on the second page
        QCOMPARE(model.count(), 1);
        const QString path = model.pathAt(0);
        model.clearFilter();
        QCOMPARE(model.count(), 2000);
        const int row = model.rowOf(path);
        QVERIFY(row >= 0);
        QCOMPARE(model.pathAt(row), path);
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
    // An explicit sort or hidden-files change shows its result in a truncated folder, through the single publish.
    void aSortChangeRepublishesATruncatedFolderOnce() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        QStringList reversed = names(model);
        std::reverse(reversed.begin(), reversed.end());
        model.select(10);
        const QStringList selected = model.selectedPaths();
        QCOMPARE(selected.size(), 1);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset), inserted(&model, &QAbstractItemModel::rowsInserted), layouts(&model, &QAbstractItemModel::layoutChanged);
        const int starts = paged.folder.starts->load();
        model.setSort("name", true, 0);
        QVERIFY(model.loading());
        QTRY_COMPARE_WITH_TIMEOUT(paged.folder.starts->load(), starts + 1, 10000);
        QVERIFY(listed(model, kPage));
        QCOMPARE(names(model), reversed);
        QCOMPARE(resets.count(), 1); QCOMPARE(inserted.count(), 0); QCOMPARE(layouts.count(), 0);
        QCOMPARE(model.selectedPaths(), selected);
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore()); // still not a whole-folder order
    }
    void aHiddenFilesToggleRepublishesATruncatedFolderOnce() {
        QTemporaryDir dir; SwitchingFolder paged(2500); paged.installSwitchedBy(true);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        QVERIFY(names(model).first() != "a-other.png");
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.setIncludeHidden(true);
        QVERIFY(model.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QCOMPARE(names(model).first(), QString("a-other.png"));
        QCOMPARE(model.count(), kPage);
        QCOMPARE(resets.count(), 1);
        QVERIFY(model.truncated());
    }
    void aSortChangeUnderAFilterReachesTheWholeListing() {
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        QStringList reversed = names(model);
        std::reverse(reversed.begin(), reversed.end());
        model.setFilter("f00020", true);
        QVERIFY(listed(model, 100));
        QStringList filtered = names(model);
        std::reverse(filtered.begin(), filtered.end());
        model.setSort("name", true, 0);
        QVERIFY(model.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QCOMPARE(names(model), filtered);
        model.clearFilter();
        QVERIFY(listed(model, kPage));
        QCOMPARE(names(model), reversed);
    }
    // File-watch refreshes stay on the old rule, so a folder that changes does not reorder under the user.
    void aRefreshAfterASortChangeDoesNotPublish() {
        QTemporaryDir dir; SwitchingFolder paged(2500); paged.installSwitchedBy(false);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.setSort("name", true, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        const QStringList shown = names(model);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        const int starts = paged.other.starts->load();
        paged.useOther->store(true);
        model.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(paged.other.starts->load(), starts + 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QCOMPARE(names(model), shown);
        QCOMPARE(resets.count(), 0);
    }
    void aRefreshDuringASortChangeWaitsForIt() {
        QTemporaryDir dir; SwitchingFolder paged(2500); paged.other.heldAfter = kPage / 2; paged.installSwitchedBy(false);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        paged.useOther->store(true);
        model.setSort("name", true, 0);
        QTRY_COMPARE_WITH_TIMEOUT(paged.other.starts->load(), 1, 10000);
        model.refresh(); // restarting here would let a busy folder hold back what the user asked for
        QTest::qWait(64); // sleep-ok: several ticks in which a second scan would start
        QCOMPARE(paged.other.starts->load(), 1);
        paged.other.release();
        QTRY_VERIFY_WITH_TIMEOUT(names(model).contains("a-other.png"), 10000); // the sort change's own rows are published
        QTRY_COMPARE_WITH_TIMEOUT(paged.other.starts->load(), 2, 10000); // the coalesced refresh
    }
    // Free memory bounds a page below the entry limit.
    void loadMoreChecksFreeMemory_data() {
        QTest::addColumn<bool>("reads");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("too little free") << true << false;
        QTest::newRow("enough free") << true << true;
        QTest::newRow("reading failed") << false << true;
    }
    void loadMoreChecksFreeMemory() {
        QFETCH(bool, reads); QFETCH(bool, allowed);
        QTemporaryDir dir; PagedFolder paged(2500); paged.install();
        directory_scan::testSeam.availableMemory = [=]() -> std::optional<std::uint64_t> {
            if (!reads) return std::nullopt;
            return allowed ? directory_scan::kMemoryHeadroomBytes * 4 : std::uint64_t(1024);
        };
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        const QStringList shown = names(model);
        const int starts = paged.folder.starts->load();
        QSignalSpy changed(&model, &DirectoryEntryModel::directoryChanged);
        QVERIFY(model.canLoadMore()); QVERIFY(!model.loadMoreRefused());
        model.loadMore();
        if (allowed) {
            QVERIFY(listed(model, 2000));
            QVERIFY(!model.loadMoreRefused()); QVERIFY(model.canLoadMore());
            model.loadMore();
            QVERIFY(listed(model, 2500)); // the entry limit still holds, and this folder fits under it
            return;
        }
        QVERIFY(model.loadMoreRefused()); QVERIFY(!model.canLoadMore());
        QCOMPARE(changed.count(), 1);
        QVERIFY(!model.loading()); QVERIFY(model.truncated());
        QCOMPARE(names(model), shown);
        QTest::qWait(64); // sleep-ok: several ticks in which a scan would start
        QCOMPARE(paged.folder.starts->load(), starts);
        model.loadMore(); // nothing to do until the folder is opened again
        QCOMPARE(paged.folder.starts->load(), starts);
    }
    void aRefusedPageIsOfferedAgainWhenTheFolderIsOpenedAgain() {
        QTemporaryDir dir, other; PagedFolder paged(2500); paged.install();
        auto spare = std::make_shared<std::atomic<std::uint64_t>>(0);
        directory_scan::testSeam.availableMemory = [spare]() -> std::optional<std::uint64_t> { return spare->load(); };
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(model.loadMoreRefused());
        QSignalSpy changed(&model, &DirectoryEntryModel::directoryChanged);
        model.refresh(); // a change in the folder is not the user asking again
        QVERIFY(model.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QVERIFY(model.loadMoreRefused());
        model.setDirectory(dir.path()); // opened again
        QVERIFY(!model.loadMoreRefused()); QVERIFY(changed.count() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
        QVERIFY(model.canLoadMore());
        model.loadMore();
        QVERIFY(model.loadMoreRefused());
        model.setDirectory(other.path());
        QVERIFY(!model.loadMoreRefused());
        QVERIFY(listed(model, kPage));
        model.loadMore();
        QVERIFY(model.loadMoreRefused());
        model.setActive(false); model.setActive(true); // the grid left and came back
        QVERIFY(!model.loadMoreRefused());
        QVERIFY(listed(model, kPage));
        spare->store(directory_scan::kMemoryHeadroomBytes * 4);
        model.loadMore();
        QVERIFY(listed(model, 2000));
    }
    // The estimate follows the rows this folder really has.
    void aFolderOfLongNamesNeedsMoreFreeMemory() {
        const std::uint64_t fits = directory_scan::kMemoryHeadroomBytes + directory_scan::kPagePeakFactor * 2000 * 1100;
        const auto tryNames = [&](int length) {
            QTemporaryDir dir; PagedFolder paged(2500);
            paged.folder.name = [length](int i) { return QString("f%1").arg(i, length, 10, QChar('0')) + ".png"; };
            paged.install();
            directory_scan::testSeam.availableMemory = [fits]() -> std::optional<std::uint64_t> { return fits; };
            DirectoryEntryModel model; model.setActive(true);
            model.setDirectory(dir.path());
            if (!listed(model, kPage)) return false;
            model.loadMore();
            return model.loadMoreRefused();
        };
        QVERIFY(!tryNames(7));
        QVERIFY(tryNames(240));
    }
    void pageFitsLeavesTheHeadroomFree() {
        const std::uint64_t needed = 500 * 2000 * directory_scan::kPagePeakFactor + directory_scan::kMemoryHeadroomBytes;
        QVERIFY(directory_scan::pageFits(needed, 500, 2000));
        QVERIFY(!directory_scan::pageFits(needed - 1, 500, 2000));
        QVERIFY(directory_scan::pageFits(std::nullopt, 500, 2000));
    }
    void averageRowBytesComesFromTheRows() {
        const auto rowsNamed = [](int length, std::size_t count) {
            std::vector<directory_scan::Row> rows(count);
            for (auto& row : rows) { row.name = QString(length, QChar('n')); row.path = "C:/" + row.name; row.key = row.path; }
            return rows;
        };
        QCOMPARE(directory_scan::averageRowBytes({}), std::uint64_t(0));
        const std::uint64_t shortNames = directory_scan::averageRowBytes(rowsNamed(10, 5000));
        const std::uint64_t longNames = directory_scan::averageRowBytes(rowsNamed(200, 5000));
        QVERIFY(shortNames >= sizeof(directory_scan::Row) + 3 * 16 + (10 + 13 + 13) * 2);
        QVERIFY(longNames >= shortNames + (190 * 3) * 2);
        auto mixed = rowsNamed(10, 5000);
        for (std::size_t i = 2500; i < mixed.size(); ++i) mixed[i] = rowsNamed(200, 1)[0];
        const std::uint64_t average = directory_scan::averageRowBytes(mixed);
        QVERIFY(average > shortNames + (longNames - shortNames) / 3 && average < longNames - (longNames - shortNames) / 3); // sampled across the rows
    }
    void linuxFreeMemoryReadsMemAvailableAndTheCgroup() {
        const QByteArray meminfo = "MemTotal:       16000000 kB\nMemFree:          100000 kB\nMemAvailable:    8000000 kB\nBuffers:           10 kB\n";
        const std::uint64_t host = std::uint64_t(8000000) * 1024;
        QCOMPARE(directory_scan::linuxAvailableBytes(meminfo, "", ""), std::optional<std::uint64_t>(host));
        QCOMPARE(directory_scan::linuxAvailableBytes(meminfo, "max\n", "4096\n"), std::optional<std::uint64_t>(host)); // no limit set
        QCOMPARE(directory_scan::linuxAvailableBytes(meminfo, "1048576\n", "4096\n"), std::optional<std::uint64_t>(1048576 - 4096)); // the group is tighter
        QCOMPARE(directory_scan::linuxAvailableBytes(meminfo, "4096\n", "1048576\n"), std::optional<std::uint64_t>(0)); // over its limit
        QCOMPARE(directory_scan::linuxAvailableBytes("MemTotal: 1 kB\n", "", ""), std::optional<std::uint64_t>{}); // nothing to read
        QCOMPARE(directory_scan::linuxAvailableBytes("MemAvailable: x kB\n", "", ""), std::optional<std::uint64_t>{});
        QCOMPARE(directory_scan::linuxAvailableBytes("", "1048576", "4096"), std::optional<std::uint64_t>(1048576 - 4096));
    }
    void thisMachineReportsFreeMemory() {
        const auto available = directory_scan::availableMemoryBytes();
        QVERIFY(available.has_value());
        QVERIFY(*available > 0);
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
    // The listing is still truncated here, so only the page itself can bring the match in.
    void aFilteredPageThatIsStillTruncatedShowsItsMatches() {
        QTemporaryDir dir; PagedFolder paged(5000); paged.install();
        DirectoryEntryModel model; model.setActive(true);
        model.setFilter("f0003500", true); // i == 1500: on the second page, with more of the folder after it
        model.setDirectory(dir.path());
        QVERIFY(listed(model, 0));
        model.loadMore();
        QVERIFY(listed(model, 1));
        QVERIFY(model.truncated()); QVERIFY(model.canLoadMore());
        model.clearFilter();
        QCOMPARE(model.count(), 2000);
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
            qint64 worstGap = 0;
            const auto step = [&](const char* name, auto&& start) {
                Tracer tracer(model, model.count() + 100000);
                tracer.begin();
                start();
                const bool done = QTest::qWaitFor([&] { return !model.loading(); }, 280000);
                tracer.finish();
                const Memory now = memoryNow();
                worstGap = std::max(worstGap, tracer.trace.worstGapMs);
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
            QVERIFY2(worstGap < kGuiGapBudgetMs, qPrintable(QString("a page stalled the GUI for %1 ms").arg(worstGap)));
            QTest::qWait(1000); // sleep-ok: the pool frees the previous page after the publish, with nothing to wait on
            qInfo().noquote() << QString("PERF_ENUM_MEMORY %1 estimate rows=%2 average_row_bytes=%3 estimated_page_peak_mb=%4")
                .arg(mode).arg(model.count()).arg(directory_scan::averageRowBytes(sampledRows(model)))
                .arg(directory_scan::averageRowBytes(sampledRows(model)) * model.count() * directory_scan::kPagePeakFactor / (1024 * 1024));
            const Memory rested = memoryNow();
            qInfo().noquote() << QString("PERF_ENUM_MEMORY %1 rested rows=%2 working_set_mb=%3 peak_mb=%4").arg(mode).arg(model.count()).arg(rested.workingSetMb).arg(rested.peakMb);
            // Typing a filter projects every listed row on the GUI thread, as it always has.
            QElapsedTimer filtering; filtering.start();
            model.setFilter("f0000500", true);
            const qint64 applied = filtering.restart();
            model.clearFilter();
            qInfo().noquote() << QString("PERF_ENUM_FILTER %1 rows=%2 set_ms=%3 clear_ms=%4").arg(mode).arg(model.count()).arg(applied).arg(filtering.elapsed());
        }
        directory_scan::testSeam = {};
    }
    // The worker ends while rows still drain, so the scan's end waits on the last tick. Crossing the budget in that tick
    // left nothing to publish it: `loading` stuck, and every refresh waited on it for good.
    void aFinishedScanIsNotLostAtTheDefinitiveBatch() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 16000;
        SeamScope seam(&folder);
        directory_scan::testSeam.beforeDryCheck = [] { std::this_thread::sleep_for(std::chrono::milliseconds(7)); }; // sleep-ok: past the drain budget, at the one moment that matters
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        // No event loop here, so the scan finishes with its rows still queued.
        QDeadlineTimer deadline(10000);
        while (folder.exits->load() < 1 && !deadline.hasExpired()) std::this_thread::yield();
        QCOMPARE(folder.exits->load(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // sleep-ok: the worker's last steps after the enumerator have nothing to wait on
        QTRY_VERIFY_WITH_TIMEOUT(!model.arriving() && model.count() == 16000, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
    }
    // QML reads cursorRow through selectionChanged, so a row that moves has to say so.
    void cursorRowIsNotifiedWhenRowsMoveAroundIt() {
        QTemporaryDir dir;
        FakeFolder folder; folder.total = 2000; folder.heldAfter = 1024;
        folder.name = [](int i) { return i % 10 == 0 ? QString("dir%1").arg(2000 - i, 5, 10, QChar('0')) : QString("f%1.png").arg(2000 - i, 5, 10, QChar('0')); };
        SeamScope seam(&folder);
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1024, 10000);
        model.select(500);
        const QString path = model.pathAt(500);
        int notified = model.cursorRow(), staleAfterInsert = 0;
        connect(&model, &DirectoryEntryModel::selectionChanged, this, [&] { notified = model.cursorRow(); });
        connect(&model, &DirectoryEntryModel::countChanged, this, [&] { if (model.arriving() && notified != model.cursorRow()) ++staleAfterInsert; });
        bool staleAtDefinitive = false;
        connect(&model, &DirectoryEntryModel::arrivingChanged, this, [&] { if (!model.arriving() && notified != model.cursorRow()) staleAtDefinitive = true; });
        folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        QCOMPARE(staleAfterInsert, 0);
        QVERIFY(!staleAtDefinitive);
        QCOMPARE(model.pathAt(notified), path);
    }
    void selectionTotalsFollowEveryChange() {
        QTemporaryDir dir;
        FakeFolder plain, fewer, held;
        const auto names = [](int i) { return i % 10 == 0 ? QString("dir%1").arg(2000 - i, 5, 10, QChar('0')) : QString("f%1.png").arg(2000 - i, 5, 10, QChar('0')); };
        plain.total = held.total = 2000; fewer.total = 1500; plain.name = fewer.name = held.name = names; held.heldAfter = 1024;
        auto source = std::make_shared<std::atomic_int>(0);
        SeamScope seam(nullptr);
        directory_scan::testSeam.enumerator = [a = plain.enumerator(), b = fewer.enumerator(), c = held.enumerator(), source](
            const std::filesystem::path& d, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return (source->load() == 0 ? a : source->load() == 1 ? b : c)(d, hidden, cancel, limit, sink);
        };
        DirectoryEntryModel model; model.setActive(true);
        model.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        // Read at every change signal too, as QML would: the totals must already be current there.
        int stale = 0;
        const auto readNow = [&] { if (std::make_pair(model.selectedCount(), model.selectedBytes()) != totalsFromRows(model)) ++stale; };
        connect(&model, &QAbstractItemModel::rowsInserted, this, readNow);
        connect(&model, &QAbstractItemModel::rowsRemoved, this, readNow);
        connect(&model, &QAbstractItemModel::modelReset, this, readNow);
        connect(&model, &QAbstractItemModel::layoutChanged, this, readNow);
        connect(&model, &QAbstractItemModel::dataChanged, this, readNow);
        connect(&model, &DirectoryEntryModel::countChanged, this, readNow);
        connect(&model, &DirectoryEntryModel::selectionChanged, this, readNow);
        connect(&model, &DirectoryEntryModel::directoryChanged, this, readNow);
        const auto check = [&](const char* when) {
            const auto expected = totalsFromRows(model);
            QVERIFY2(model.selectedCount() == expected.first, when);
            QVERIFY2(model.selectedBytes() == expected.second, when);
            QVERIFY2(model.selectedPaths() == pathsFromRows(model), when);
        };
        check("nothing selected");
        model.select(10); model.select(1999, DirectoryEntryModel::Toggle); model.select(1500, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 3);
        check("three selected");
        model.setFilter("f01", true);
        QTRY_VERIFY_WITH_TIMEOUT(model.count() > 0 && model.count() < 2000, 10000);
        check("filtered");
        model.clearFilter();
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 2000, 10000);
        check("filter cleared");
        model.selectAll();
        check("all selected");
        source->store(1);
        model.refresh(); // 500 rows go, selected ones among them, without a selection command
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 1500, 10000);
        QCOMPARE(model.selectedCount(), 1500);
        check("rows removed");
        model.setActive(false);
        check("inactive");
        source->store(2);
        model.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(model.count() == 1024 && model.arriving(), 10000);
        check("arriving");
        held.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        check("arrived");
        QCOMPARE(stale, 0);
    }
    void aTickThatBringsTheCursorsRowNotifiesIt() {
        CursorStillToArrive f; QVERIFY(f.start(0));
        DirectoryEntryModel& model = *f.model;
        int notified = model.cursorRow(), stale = 0;
        connect(&model, &DirectoryEntryModel::selectionChanged, this, [&] { notified = model.cursorRow(); });
        connect(&model, &DirectoryEntryModel::countChanged, this, [&] { if (model.arriving() && notified != model.cursorRow()) ++stale; });
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1024, 10000);
        QCOMPARE(model.cursorRow(), -1);
        f.held.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        QCOMPARE(stale, 0);
        QCOMPARE(model.pathAt(notified), f.path);
    }
    void folderTicksBeforeTheCursorArrivesAreNotAnnounced() {
        CursorStillToArrive f; QVERIFY(f.start(10));
        DirectoryEntryModel& model = *f.model;
        int needless = 0;
        connect(&model, &DirectoryEntryModel::selectionChanged, this, [&] { if (model.count() > 0 && model.cursorRow() < 0) ++needless; });
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1024, 10000);
        QCOMPARE(needless, 0);
        f.held.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() == 2000, 10000);
        QCOMPARE(model.pathAt(model.cursorRow()), f.path);
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
