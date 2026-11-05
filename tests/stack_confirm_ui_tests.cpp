// STACK-CONFIRM-FIX: regressions found by the whole-stack confirmation review (docs/verification/STACK-CONFIRM-REVIEW.md).
// (A vanished-file slideshow case was tried and dropped: the viewer relists at once, so the vanished name never reaches a decode;
//  the undecodable-photo case exercises the same fail() path and is the one that fails on the base.)
//   B-1 slideshow over undecodable or vanished photos, N-2 slideshow ends on tab and folder navigation, G-1 details mode keeps the
//   visible anchor, N-1 type chips on the keyboard, N-5 long tab names. One executable, offscreen, like the other *_ui tests.
#include "ui_test_support.hpp"
#include <QAccessible>
#include <QAccessibleInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../src/app/fs_monitor.hpp"
#include "../src/app/transfer_segment.hpp"
#ifdef Q_OS_WIN
#include <psapi.h>
#endif

namespace {
QQuickItem* noticeItem(Harness& h) { return named(h.window, "deleteNotice"); }
QString noticeText(Harness& h) { auto* item = noticeItem(h); return item ? item->property("text").toString() : QString(); }
void setSlideshowSeconds(Harness& h, int seconds) {
    QVERIFY(QMetaObject::invokeMethod(h.window, "setSlideshowSeconds", Q_ARG(QVariant, seconds)));
}
// Lets the window render a few frames (layout and polish settle) by waiting on a condition, never on a fixed sleep.
void frames(Harness& h, int count = 3) {
    int seen = 0;
    const auto c1 = QObject::connect(h.window, &QQuickWindow::frameSwapped, h.window, [&] { ++seen; });
    const auto c2 = QObject::connect(h.window, &QQuickWindow::afterAnimating, h.window, [&] { ++seen; });
    const auto c3 = QObject::connect(h.window, &QQuickWindow::beforeSynchronizing, h.window, [&] { ++seen; });
    QTRY_VERIFY_WITH_TIMEOUT([&] { h.window->requestUpdate(); return seen >= count; }(), 10000);
    QObject::disconnect(c1); QObject::disconnect(c2); QObject::disconnect(c3);
}
// Ends a slideshow and lets any step it had in flight land before the harness and its folders are torn down.
void settleSlideshow(Harness& h) {
    QMetaObject::invokeMethod(h.window, "stopSlideshow");
    QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.loading(), 10000);
}
void overwriteWithText(const QString& path) {
    QFile bad(path);
    QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Truncate));
    bad.write("not an image, just bytes");
}
// A folder of small disposable photos below `base`.
QString makeFolder(const QString& base, const QString& name, int photos) {
    const QString folder = QDir(base).filePath(name);
    if (!QDir().mkpath(folder)) return {};
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(QColor::fromHsv(int(qHash(name) % 360), 120, 180));
    for (int i = 0; i < photos; ++i)
        if (!image.save(QDir(folder).filePath(QString("%1-%2.png").arg(name.left(1)).arg(i + 1)))) return {};
    return folder;
}
// Where `item` sits in `strip`'s own coordinates.
QRectF inStrip(QQuickItem* strip, QQuickItem* item) { return item->mapRectToItem(strip, QRectF(0, 0, item->width(), item->height())); }
bool fullyInside(QQuickItem* strip, QQuickItem* item) {
    const QRectF r = inStrip(strip, item);
    return r.left() >= -0.5 && r.right() <= strip->width() + 0.5 && r.top() >= -0.5 && r.bottom() <= strip->height() + 0.5;
}
// Proves the event loop keeps running while decoders die around it. A blocked GUI thread stops the beat, and the watchdog then
// ends the run with a clear message (the old freeze never returned, so no assertion after it would ever run).
struct Heartbeat {
    QTimer timer; QElapsedTimer sinceLast; qint64 maxGap{}; int beats{};
    std::atomic<qint64> lastBeatMs{};
    std::mutex mutex; std::condition_variable stopping; bool stop{};
    std::thread watchdog;
    QElapsedTimer clock;
    explicit Heartbeat(int blockedMs = 30000) {
        clock.start(); sinceLast.start();
        QObject::connect(&timer, &QTimer::timeout, [this] { maxGap = std::max(maxGap, sinceLast.restart()); ++beats; lastBeatMs = clock.elapsed(); });
        timer.start(10);
        watchdog = std::thread([this, blockedMs] {
            std::unique_lock lock(mutex);
            while (!stopping.wait_for(lock, std::chrono::milliseconds(250), [this] { return stop; }))
                if (clock.elapsed() - lastBeatMs.load() > blockedMs) qFatal("The GUI thread has been blocked for %d ms: the viewer is frozen.", blockedMs);
        });
    }
    ~Heartbeat() { { std::lock_guard lock(mutex); stop = true; } stopping.notify_all(); watchdog.join(); }
};
// How many mappings of exactly one transfer's size this process holds: the memory the viewer has not given back.
int mappedTransfers(qint64 bytes) {
    const qint64 rounded = (bytes + 4095) / 4096 * 4096;
    int count = 0;
#ifdef Q_OS_WIN
    MEMORY_BASIC_INFORMATION region{};
    for (const char* at = nullptr; VirtualQuery(at, &region, sizeof(region)) == sizeof(region); at = static_cast<const char*>(region.BaseAddress) + region.RegionSize)
        if (region.Type == MEM_MAPPED && region.State == MEM_COMMIT && qint64(region.RegionSize) == rounded) ++count;
#else
    QFile maps("/proc/self/maps");
    if (!maps.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : maps.readAll().split('\n')) {
        if (!line.contains("/SYSV")) continue;
        const auto range = line.left(line.indexOf(' ')).split('-');
        if (range.size() == 2 && range[1].toLongLong(nullptr, 16) - range[0].toLongLong(nullptr, 16) == rounded) ++count;
    }
#endif
    return count;
}
// The fixture decoder (tests/fs_watch_decoder_fixture.cpp) in place of the real one, for one viewer.
struct DecoderFixture {
    QTemporaryDir control, markers;
    fs_watch::ControllerTestHooks hooks;
    int prefetches{};
    bool configure(QJsonObject config) {
        config["markerDir"] = markers.path();
        QFile file(control.filePath("worker.json"));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(config).toJson(QJsonDocument::Compact)) > 0;
    }
    DecoderFixture() {
#ifdef Q_OS_WIN
        hooks.decoderProgram = QCoreApplication::applicationDirPath() + "/fs_watch_decoder_fixture.exe";
#else
        hooks.decoderProgram = QCoreApplication::applicationDirPath() + "/fs_watch_decoder_fixture";
#endif
        hooks.decoderArguments = {"--fixture-config", control.filePath("worker.json")};
        hooks.workerStarted = [this](QProcess*, const QString& kind, const QString&) { if (kind == "prefetch") ++prefetches; };
        fs_watch::controllerTestHooks = &hooks; // copied when the viewer is constructed
    }
    ~DecoderFixture() { fs_watch::controllerTestHooks = nullptr; }
    QStringList keys() const { return QDir(markers.path()).entryList(QDir::Files); }
    bool attached(const QString& path) const {
        for (const QString& key : keys()) {
            QFile marker(QDir(markers.path()).filePath(key));
            if (marker.open(QIODevice::ReadOnly) && QDir::fromNativeSeparators(QString::fromUtf8(marker.readAll())) == QDir::fromNativeSeparators(path)) return true;
        }
        return false;
    }
};
// Whether any process still holds the transfer with this key. Attaching succeeds only if something kept the memory alive. (Not
// for polling on System V, where the last detach removes a segment: a probe would clean up the very orphan it should report.)
bool transferStillExists(const QString& key) { glancelot::detail::TransferSegment probe(key); return probe.attach(); }
#ifndef Q_OS_WIN
// System V segments of exactly this size on the whole machine: one nobody removed outlives every process.
int systemSegments(qint64 bytes) {
    QFile list("/proc/sysvipc/shm");
    if (!list.open(QIODevice::ReadOnly)) return -1;
    int count = 0;
    const auto lines = list.readAll().split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const auto columns = lines[i].simplified().split(' ');
        if (columns.size() > 3 && columns[3].toLongLong() == bytes) ++count;
    }
    return count;
}
#endif
} // namespace

class StackConfirmUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir* config{};
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr; }

    // B-1: a photo that cannot be decoded is "shown" once it has failed: the slideshow steps past it and keeps going.
    void slideshowSkipsAnUndecodablePhoto() {
        Media media(3, QSize(64, 48));
        overwriteWithText(media.path(1));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 1, 10000);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 3, 10000); // past the undecodable second photo
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 10000);
        QVERIFY(flag(h, "slideshowOn"));
        // And on round the loop, never stuck: back at the first photo, then past the bad one again.
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 3, 10000);
        QVERIFY(flag(h, "slideshowOn"));
    }
    // B-1: a run of failures is bounded: the slideshow stops and says why instead of cycling through errors.
    void slideshowStopsAfterAnUnplayableRun() {
        Media media(8, QSize(64, 48));
        for (int i = 1; i < 8; ++i) overwriteWithText(media.path(i));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 1, 10000);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 20000);
        QVERIFY2(noticeText(h).contains("Slideshow stopped"), qPrintable(noticeText(h)));
        QCOMPARE(h.viewer.currentIndex(), 6); // five failed photos in a row (2 to 6), then it stopped
        auto* indicator = named(h.window, "slideshowIndicator");
        QVERIFY(!indicator || !indicator->isVisible());
    }
    // B-1 guard: a slideshow that crosses into the next folder by itself (boundary "continue") is not "manual navigation".
    void slideshowKeepsGoingAcrossAFolderBoundary() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString a = makeFolder(root.path(), "a", 2), b = makeFolder(root.path(), "b", 2);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(a).filePath("a-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000); // the default root is the opened folder; widen it so its siblings are reachable
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()).endsWith("/b"), 10000);
        QTest::qWait(300); // sleep-ok: asserting the slideshow does not end just after its own step crossed the folder boundary
        QVERIFY2(flag(h, "slideshowOn"), "the slideshow's own step into the next folder ended it");
        settleSlideshow(h);
    }
    // R2-N1: a failed photo is passed after about a second even at a slow pace (it is not held for a whole interval).
    void slideshowPassesABadPhotoPromptlyAtASlowPace() {
        Media media(3, QSize(64, 48));
        overwriteWithText(media.path(1));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 1, 10000);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 6);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 2, 10000); // the bad photo is reached on the 6 s tick
        QElapsedTimer sinceBad; sinceBad.start();
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 3, 10000); // long before the next 6 s tick
        QVERIFY2(sinceBad.elapsed() < 3500, qPrintable(QString::number(sinceBad.elapsed())));
        QVERIFY(flag(h, "slideshowOn"));
    }
    // R2-B1: under the default "continue" boundary the slideshow passes through a folder with a single photo, and a step
    // from one folder's only photo to the next folder's first photo is not mistaken for a dead end.
    void slideshowPassesThroughAOnePhotoFolder() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString a = makeFolder(root.path(), "a", 2), b = makeFolder(root.path(), "b", 1), c = makeFolder(root.path(), "c", 2);
        QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(a).filePath("a-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.fileName(), QString("c-2.png"), 15000);
        QVERIFY2(flag(h, "slideshowOn"), "the slideshow stopped on its way through the one-photo folder");
        QVERIFY2(noticeText(h).isEmpty(), qPrintable(noticeText(h)));
        settleSlideshow(h);
    }
    // R2-N2: closing a tab other than the active one is not navigation.
    void slideshowSurvivesClosingAnotherTab() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        h.viewer.trigger("tab-new"); QTRY_COMPARE_WITH_TIMEOUT(int(h.viewer.tabs().size()), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.activeTab(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 10000);
        setSlideshowSeconds(h, 5);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QVERIFY(h.viewer.closeTab(0)); // the active tab becomes number 0
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.activeTab(), 0, 10000);
        frames(h);
        QVERIFY2(flag(h, "slideshowOn"), "closing an inactive tab ended the slideshow");
        settleSlideshow(h);
    }
    // R4-B2: a segment lives until its last handle closes, whoever opened it, and not a moment longer.
    void aTransferSegmentLivesUntilItsLastHandleCloses() {
        namespace d = glancelot::detail;
        const QString key = d::newTransferKey();
        auto creator = std::make_unique<d::TransferSegment>(key);
        QVERIFY(!transferStillExists(key));
        QVERIFY2(creator->create(4096), qPrintable(creator->errorString()));
        QVERIFY(creator->size() >= 4096);
        *static_cast<unsigned char*>(creator->data()) = 0xA5;
        auto worker = std::make_unique<d::TransferSegment>(key);
        QVERIFY2(worker->attach(), qPrintable(worker->errorString()));
        QVERIFY(worker->size() >= 4096);
        QCOMPARE(*static_cast<const unsigned char*>(worker->constData()), (unsigned char)0xA5);
#ifdef Q_OS_WIN
        // The creator's share is closed first, as when the viewer cancels: the worker's mapping stays valid. (System V keeps the
        // attached segment too, but a later attach by key finds it only until the last detach, so this holds on Windows alone.)
        creator.reset();
        QVERIFY(transferStillExists(key));
        QCOMPARE(*static_cast<const unsigned char*>(worker->constData()), (unsigned char)0xA5);
        worker.reset();
#else
        worker.reset();
        creator.reset();
#endif
        QVERIFY2(!transferStillExists(key), "the memory outlived its last handle");
    }
    // A key belongs to one transfer: a second creator is refused, never handed the first one's memory.
    void aTransferSegmentRefusesAKeyInUse() {
        namespace d = glancelot::detail;
        const QString key = d::newTransferKey();
        d::TransferSegment first(key), second(key);
        QVERIFY(first.create(4096));
        QVERIFY(!second.create(4096));
        QVERIFY(!second.errorString().isEmpty());
    }
    void aTransferSegmentReportsFailures() {
        namespace d = glancelot::detail;
        d::TransferSegment missing(d::newTransferKey()), empty(d::newTransferKey());
        QVERIFY(!missing.attach());
        QVERIFY(!missing.errorString().isEmpty());
        QVERIFY(!empty.create(0));
        QVERIFY(!empty.errorString().isEmpty());
    }
    // R4-B1, R4-B2: a decoder that dies or is killed mid-transfer costs the viewer nothing. It answers the probe, attaches, then
    // ends at each point of the transfer while holding the lock of the old QSharedMemory transport (see the fixture). The window
    // keeps running, and none of the memory stays: not in this process, and not anywhere (the key no longer opens).
    void aDecoderDyingMidTransferNeitherBlocksNorKeepsMemory_data() {
        QTest::addColumn<QString>("dieAt"); QTest::addColumn<bool>("killed"); QTest::addColumn<bool>("shown"); QTest::addColumn<QString>("boundary");
        QTest::newRow("dies right after attaching") << "attached" << false << false << "stop";
        QTest::newRow("dies after the viewer's ok") << "acknowledged" << false << true << "stop";
        QTest::newRow("killed by the viewer while holding the lock") << "" << true << false << "stop";
        QTest::newRow("prefetch decoders die after the ok too") << "acknowledged" << false << true << "continue";
    }
    void aDecoderDyingMidTransferNeitherBlocksNorKeepsMemory() {
        QFETCH(QString, dieAt); QFETCH(bool, killed); QFETCH(bool, shown); QFETCH(QString, boundary);
        const int width = 2000, height = 1500, steps = 18;
        const qint64 bytes = qint64(width) * height * 4;
        QTemporaryDir root; QVERIFY(root.isValid());
        QStringList photos;
        // Two photos a folder keeps every step at a folder's edge, which is where the viewer prefetches.
        for (int i = 0; i <= steps / 2; ++i) {
            const QString name = QString("f%1").arg(i, 2, 10, QLatin1Char('0'));
            const QString folder = makeFolder(root.path(), name, 2); QVERIFY(!folder.isEmpty());
            photos << QDir(folder).filePath("f-1.png") << QDir(folder).filePath("f-2.png");
        }
        DecoderFixture fixture;
        QJsonObject config{{"width", width}, {"height", height}};
        if (!dieAt.isEmpty()) config["dieAt"] = dieAt;
        if (killed) { config["holdLock"] = true; config["decodeDelayMs"] = 60000; }
        QVERIFY(fixture.configure(config));
        const int mappedBefore = mappedTransfers(bytes);
#ifndef Q_OS_WIN
        const int systemBefore = systemSegments(bytes);
#endif
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        Heartbeat beat;
        int peak = 0;
        const auto observed = [&] { peak = std::max(peak, mappedTransfers(bytes) - mappedBefore); };
        QTimer watch; QObject::connect(&watch, &QTimer::timeout, observed); watch.start(50);
        h.viewer.setBoundaryMode(boundary);
        h.viewer.open(photos[0]);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.fileName() == QString("f-1.png") && !h.viewer.navigating(), 15000);
        if (boundary == "continue") { QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000); h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000); }
        for (int step = 0; step < steps; ++step) {
            const QString from = h.viewer.filePath();
            if (killed) QTRY_VERIFY_WITH_TIMEOUT(fixture.attached(from), 15000); // it holds the lock now
            else QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.loading(), 15000);
            // A prefetch starts only some time after a photo lands, and stepping on cancels it: let the first few really run. Each
            // folder needs one: it fetches the next folder's first photo, which is then cached for the folder's other photo.
            if (boundary == "continue" && fixture.prefetches < 3) QTRY_VERIFY2_WITH_TIMEOUT(fixture.prefetches > step / 2, "no prefetch started", 10000);
            if (boundary == "stop") h.viewer.open(photos[step + 1]); else h.viewer.next();
            QTRY_VERIFY_WITH_TIMEOUT(h.viewer.filePath() != from, 15000);
            QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 15000);
            if (killed) continue; // this one is killed in its turn
            QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.loading(), 15000);
            QCOMPARE(h.viewer.imageReady(), shown);
        }
        if (killed) QTRY_VERIFY_WITH_TIMEOUT(fixture.attached(h.viewer.filePath()), 15000);
        const int beatsBefore = beat.beats;
        QVERIFY(fixture.configure({})); // a last, small, healthy decode retires whatever still runs, as moving on does
        h.viewer.open(photos.last());
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && !h.viewer.navigating(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(beat.beats > beatsBefore + 10, 10000); // the event loop runs on after all of those deaths
        QVERIFY2(beat.maxGap < 15000, qPrintable(QString("the GUI thread stalled for %1 ms").arg(beat.maxGap)));
        const QStringList keys = fixture.keys();
        QVERIFY2(keys.size() >= steps, qPrintable(QString::number(keys.size()))); // that many decoders really attached
        // At most the foreground and a prefetch at a time, plus any whose process is still on its way out.
        QVERIFY2(peak <= 6, qPrintable(QString("%1 segments mapped at once: deaths are piling up").arg(peak)));
        QTRY_COMPARE_WITH_TIMEOUT(mappedTransfers(bytes) - mappedBefore, 0, 15000);
#ifdef Q_OS_WIN
        for (const QString& key : keys) QTRY_VERIFY2_WITH_TIMEOUT(!transferStillExists(key), qPrintable("still alive: " + key), 15000);
#else
        QTRY_COMPARE_WITH_TIMEOUT(systemSegments(bytes) - systemBefore, 0, 15000); // none left behind for the whole machine
#endif
    }
    // Closing the viewer while a decoder holds a transfer leaves nothing behind either.
    void closingTheViewerMidTransferLeavesNoMemoryBehind() {
        const int width = 2000, height = 1400;
        const qint64 bytes = qint64(width) * height * 4;
        Media media(2, QSize(64, 48));
        DecoderFixture fixture;
        QVERIFY(fixture.configure({{"width", width}, {"height", height}, {"holdLock", true}, {"decodeDelayMs", 60000}}));
#ifndef Q_OS_WIN
        const int systemBefore = systemSegments(bytes);
#endif
        {
            Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
            h.viewer.open(media.path(0));
            QTRY_VERIFY_WITH_TIMEOUT(fixture.attached(h.viewer.filePath()), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(mappedTransfers(bytes) > 0, 15000); // a late file event can restart the decode
        }
        const QStringList keys = fixture.keys(); QVERIFY(!keys.isEmpty());
#ifdef Q_OS_WIN
        for (const QString& key : keys) QTRY_VERIFY2_WITH_TIMEOUT(!transferStillExists(key), qPrintable("still alive: " + key), 15000);
#else
        // At once, with no event loop in between: a closing application has none left to release the segment later.
        QCOMPARE(systemSegments(bytes) - systemBefore, 0);
#endif
    }
    // The slideshow's own walk with real decoders, for the rate acceptance (R4-B2): run by hand, with GL_STACK_RATE_PASSES and
    // GL_STACK_RATE_PACE_MS. No test hook: real decoders, retired by the viewer at real moments. Every pass walks 40 folders of
    // 2 photos under "continue", so every step has a prefetch that ends in "ok" and a kill, the race that used to hang.
    void slideshowPatternRateRun() {
        const int passes = qEnvironmentVariableIntValue("GL_STACK_RATE_PASSES"), paceMs = qEnvironmentVariableIntValue("GL_STACK_RATE_PACE_MS");
        if (passes <= 0) QSKIP("set GL_STACK_RATE_PASSES to run it");
        {
            // The GUI thread's part of a teardown at the largest admitted size, with every page touched.
            const qint64 largest = 256LL * 1024 * 1024;
            auto segment = std::make_unique<glancelot::detail::TransferSegment>(glancelot::detail::newTransferKey());
            QVERIFY(segment->create(largest));
            std::fill_n(static_cast<char*>(segment->data()), largest, char(1));
            QElapsedTimer teardown; teardown.start();
            segment.reset();
            qInfo("tearing down a full 256 MiB transfer took %lld us", teardown.nsecsElapsed() / 1000);
        }
        const QSize photo(640, 480); const qint64 bytes = qint64(photo.width()) * photo.height() * 4;
        QTemporaryDir root; QVERIFY(root.isValid());
        QStringList folders;
        for (int i = 0; i < 40; ++i) {
            const QString folder = QDir(root.path()).filePath(QString("f%1").arg(i, 2, 10, QLatin1Char('0')));
            QVERIFY(QDir().mkpath(folder));
            QImage image(photo, QImage::Format_RGB32); image.fill(QColor::fromHsv(i * 9, 120, 180));
            QVERIFY(image.save(QDir(folder).filePath("f-1.png"))); QVERIFY(image.save(QDir(folder).filePath("f-2.png")));
            folders << folder;
        }
        const int mappedBefore = mappedTransfers(bytes);
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        Heartbeat beat;
        h.viewer.setBoundaryMode("continue");
        h.viewer.open(QDir(folders[0]).filePath("f-1.png"));
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && !h.viewer.navigating(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        QElapsedTimer total; total.start();
        int steps = 0, undecoded = 0;
        for (int pass = 0; pass < passes; ++pass) {
            h.viewer.open(QDir(folders[0]).filePath("f-1.png"));
            QVERIFY(QTest::qWaitFor([&] { return !h.viewer.navigating() && !h.viewer.loading(); }, 30000));
            const int stepsBefore = steps;
            for (;;) {
                QTest::qWait(paceMs + QRandomGenerator::global()->bounded(20)); // sleep-ok: the pace between steps is what this run varies
                const QString from = h.viewer.filePath();
                h.viewer.next();
                QVERIFY(QTest::qWaitFor([&] { return !h.viewer.navigating() && !h.viewer.loading(); }, 30000));
                if (h.viewer.filePath() == from) break;
                ++steps; if (!h.viewer.imageReady()) ++undecoded;
            }
            QVERIFY2(steps > stepsBefore + 70, qPrintable(QString("pass %1 walked %2 steps").arg(pass).arg(steps - stepsBefore)));
            qint64 privateBytes = 0, handles = 0;
#ifdef Q_OS_WIN
            PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) privateBytes = qint64(counters.PrivateUsage);
            DWORD handleCount = 0; if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) handles = handleCount;
#endif
            QVERIFY(QTest::qWaitFor([&] { return mappedTransfers(bytes) == mappedBefore; }, 10000));
            qInfo("pass %d: %d steps, %lld ms, mapped %d, private %lld MiB, handles %lld, undecoded %d, max GUI gap %lld ms", pass, steps, total.elapsed(),
                  mappedTransfers(bytes) - mappedBefore, privateBytes / (1024 * 1024), handles, undecoded, beat.maxGap);
        }
        qInfo("RESULT pace %d: %d steps in %lld ms, undecoded %d, max GUI gap %lld ms, mapped at the end %d", paceMs, steps, total.elapsed(), undecoded, beat.maxGap, mappedTransfers(bytes) - mappedBefore);
    }
    // R3-B1: `navigating` is true from the request of a step until it has settled, and false again afterwards.
    void navigatingSpansAStep() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        const QString first = h.viewer.filePath();
        h.viewer.next();
        QVERIFY(h.viewer.navigating());
        QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        QVERIFY(h.viewer.filePath() != first);
    }
    // R3-B1: a step of the slideshow's own that takes longer than the pace (a walk over many empty folders) is not a dead end:
    // the slideshow waits for it, then goes on, and never ends with the step still landing.
    void slideshowWaitsForASlowFolderStep_data() { QTest::addColumn<int>("seconds"); QTest::addColumn<int>("emptyFolders"); QTest::newRow("1 s pace") << 1 << 300; QTest::newRow("5 s pace") << 5 << 600; }
    void slideshowWaitsForASlowFolderStep() {
        QFETCH(int, seconds); QFETCH(int, emptyFolders);
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString a = makeFolder(root.path(), "a", 2);
        for (int i = 0; i < emptyFolders; ++i) QVERIFY(QDir(root.path()).mkpath(QString("b%1").arg(i, 4, 10, QLatin1Char('0'))));
        const QString c = makeFolder(root.path(), "c", 2);
        QVERIFY(!a.isEmpty() && !c.isEmpty());
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(a).filePath("a-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, seconds);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QStringList shown{h.viewer.fileName()}; // R4-N1: extra ticks during the step would queue steps and skip photos
        QObject::connect(&h.viewer, &ViewerController::stateChanged, &h.viewer, [&] {
            if (h.viewer.imageReady() && h.viewer.fileName() != shown.last()) shown << h.viewer.fileName();
        });
        bool leftOnWhileLanding = false;
        QElapsedTimer total; total.start();
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            if (!flag(h, "slideshowOn") && h.viewer.fileName() != QString("c-2.png")) leftOnWhileLanding = true;
            return h.viewer.fileName() == QString("c-2.png") || leftOnWhileLanding;
        }(), 120000);
        QVERIFY2(!leftOnWhileLanding, qPrintable(QString("the slideshow ended at %1 (navigating %2)").arg(h.viewer.fileName()).arg(h.viewer.navigating())));
        QVERIFY(flag(h, "slideshowOn"));
        QVERIFY2(noticeText(h).isEmpty(), qPrintable(noticeText(h)));
        // Then it ends at the true dead end (the last photo of the last folder), with the step settled and nothing landing later.
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 60000);
        const QString at = h.viewer.fileName();
        QTest::qWait(1500); // sleep-ok: asserting nothing lands after the slideshow ended
        QCOMPARE(h.viewer.fileName(), at);
        QVERIFY(!h.viewer.navigating());
        QCOMPARE(shown, QStringList({"a-1.png", "a-2.png", "c-1.png", "c-2.png"}));
        settleSlideshow(h);
    }
    // R4-N2: each photo gets the full pace, counted from when it shows, however long the step to it took. Three slow steps, because
    // one could land just after a tick by chance.
    void slideshowShowsAPhotoForTheFullPaceAfterASlowStep() {
        const int emptyFolders = 150, paceMs = 1000;
        QTemporaryDir root; QVERIFY(root.isValid());
        QStringList names{"a", "c", "e", "g"};
        for (const QString& name : names) QVERIFY(!makeFolder(root.path(), name, 2).isEmpty());
        for (const char* gap : {"b", "d", "f"})
            for (int i = 0; i < emptyFolders; ++i) QVERIFY(QDir(root.path()).mkpath(QString("%1%2").arg(gap).arg(i, 4, 10, QLatin1Char('0'))));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(root.path()).filePath("a/a-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, paceMs / 1000);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QElapsedTimer clock; clock.start();
        QHash<QString, qint64> shownAt, shownFor;
        QString current = h.viewer.fileName();
        shownAt[current] = clock.elapsed();
        QObject::connect(&h.viewer, &ViewerController::stateChanged, &h.viewer, [&] {
            const QString name = h.viewer.fileName();
            if (name != current) { if (shownAt.contains(current)) shownFor[current] = clock.elapsed() - shownAt[current]; current = name; }
            if (h.viewer.imageReady() && !shownAt.contains(name)) shownAt[name] = clock.elapsed();
        });
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 120000); // the true end, after g-2
        QCOMPARE(h.viewer.fileName(), QString("g-2.png"));
        for (const char* photo : {"c-1.png", "e-1.png", "g-1.png"}) {
            QVERIFY2(shownFor.contains(photo), photo);
            QVERIFY2(shownFor[photo] >= paceMs - 200, qPrintable(QString("%1 was on screen for %2 ms at a %3 ms pace").arg(photo).arg(shownFor[photo]).arg(paceMs)));
        }
        settleSlideshow(h);
    }
    // R4-N3: a folder opened by hand while the slideshow's own step is running ends the slideshow. Its directory change arrives
    // while a step is in flight, so without the controller's notice it was taken for that step's own.
    void slideshowEndsOnAManualOpenDuringItsOwnStep_data() {
        QTest::addColumn<QString>("route");
        QTest::newRow("openFolder") << "openFolder"; QTest::newRow("navigateDirectory") << "navigateDirectory"; QTest::newRow("open a photo") << "open";
    }
    void slideshowEndsOnAManualOpenDuringItsOwnStep() {
        QFETCH(QString, route);
        QTemporaryDir root, other; QVERIFY(root.isValid() && other.isValid());
        const QString a = makeFolder(root.path(), "a", 2), z = makeFolder(other.path(), "z", 3);
        QVERIFY(!a.isEmpty() && !z.isEmpty());
        for (int i = 0; i < 300; ++i) QVERIFY(QDir(root.path()).mkpath(QString("b%1").arg(i, 4, 10, QLatin1Char('0'))));
        QVERIFY(!makeFolder(root.path(), "c", 2).isEmpty());
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(a).filePath("a-2.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.navigating(), 10000); // its own step across the empty folders is in flight
        QVERIFY(!h.window->property("slideshowStepFrom").toString().isEmpty());
        if (route == "openFolder") h.viewer.openFolder(z);
        else if (route == "navigateDirectory") h.viewer.navigateDirectory(z);
        else h.viewer.open(QDir(z).filePath("z-2.png"));
        QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()).endsWith("/z") && !h.viewer.navigating() && !h.viewer.loading(), 15000);
        QVERIFY2(!flag(h, "slideshowOn"), "the manual open was taken for the slideshow's own step");
        settleSlideshow(h);
    }
    // R3-N3: the failure bound does not depend on the folder a step lands in: an undecodable photo alone in a folder, between
    // good folders, is passed under "continue" (it used to be a bound of 1 with "the last 1 photos").
    void slideshowPassesAnUndecodableLoneFolderPhoto() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QString a = makeFolder(root.path(), "a", 2), b = makeFolder(root.path(), "b", 1), c = makeFolder(root.path(), "c", 2);
        QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());
        overwriteWithText(QDir(b).filePath("b-1.png"));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(a).filePath("a-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h)); focusCanvas(h);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.canRaiseRoot(), 10000);
        h.viewer.raiseRoot(); QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.navigating(), 10000);
        h.viewer.setBoundaryMode("continue");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.fileName(), QString("c-2.png"), 20000);
        QVERIFY2(flag(h, "slideshowOn"), "the slideshow stopped at the undecodable lone photo");
        QVERIFY2(noticeText(h).isEmpty(), qPrintable(noticeText(h)));
        settleSlideshow(h);
    }
    // N-2: another tab, or another folder opened by hand, ends the slideshow (and its indicator).
    void slideshowEndsOnTabSwitchAndFolderOpen() {
        QTemporaryDir other; QVERIFY(other.isValid());
        const QString folder = makeFolder(other.path(), "other", 3); QVERIFY(!folder.isEmpty());
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        setSlideshowSeconds(h, 5);
        auto invoke = [&](const char* id) { QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QString(id)))); };
        auto indicatorShown = [&] { auto* i = named(h.window, "slideshowIndicator"); return i && i->isVisible(); };
        invoke("slideshow"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000); QTRY_VERIFY_WITH_TIMEOUT(indicatorShown(), 10000);
        invoke("tab-new");
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 10000); QTRY_VERIFY_WITH_TIMEOUT(!indicatorShown(), 10000);
        // Opening another folder in the same tab (tree, breadcrumbs and the path field all end here).
        h.viewer.openFolder(folder);
        QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()) == QDir::fromNativeSeparators(folder) && h.viewer.imageReady(), 10000);
        invoke("slideshow"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        h.viewer.openFolder(media.dir.path()); QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()) == QDir::fromNativeSeparators(media.dir.path()), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 10000); QTRY_VERIFY_WITH_TIMEOUT(!indicatorShown(), 10000);
        // Switching back to the first tab ends it too.
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 10000);
        invoke("slideshow"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "slideshowOn"), 10000);
        invoke("tab-previous");
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 10000); QTRY_VERIFY_WITH_TIMEOUT(!indicatorShown(), 10000);
    }

    // G-1: changing the grid mode keeps the item that was first in view in view, at any column count.
    void detailsToggleKeepsTheVisibleAnchor() {
        Media media(120, QSize(32, 24)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "gridMode"), 10000); QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 120, 10000);
        auto* view = named(h.window, "photoGridView"); QVERIFY(view);
        QObject* grid = h.object("photoGrid"); QVERIFY(grid);
        auto firstVisible = [&] {
            int index = -1;
            QMetaObject::invokeMethod(view, "indexAt", Q_RETURN_ARG(int, index), Q_ARG(qreal, 8), Q_ARG(qreal, view->property("contentY").toReal() + 4));
            return index;
        };
        auto details = [&](bool on) {
            h.viewer.trigger("view-details");
            QTRY_COMPARE_WITH_TIMEOUT(grid->property("details").toBool(), on, 10000);
            frames(h);
        };
        frames(h);
        for (const int width : {1180, 1800, 760}) {
            h.window->resize(width, 780); frames(h);
            const qreal fifth = (view->property("contentHeight").toReal() - view->height()) / 5;
            view->setProperty("contentY", view->property("originY").toReal() + fifth); frames(h);
            const int before = firstVisible(), columns = grid->property("columns").toInt();
            QVERIFY(columns > 1 && before > 0);
            details(true);
            QVERIFY2(firstVisible() == before, qPrintable(QString("width %1 (%2 columns): first visible %3 before, %4 in details").arg(width).arg(columns).arg(before).arg(firstVisible())));
            details(false);
            QVERIFY2(firstVisible() == before, qPrintable(QString("width %1 (%2 columns): first visible %3 before, %4 back in thumbnails").arg(width).arg(columns).arg(before).arg(firstVisible())));
            view->setProperty("contentY", view->property("originY").toReal()); frames(h);
        }
        // A cursor that was in view stays in view.
        h.window->resize(1800, 780); frames(h);
        const qreal middle = (view->property("contentHeight").toReal() - view->height()) / 2;
        view->setProperty("contentY", view->property("originY").toReal() + middle); frames(h);
        const int cursor = firstVisible() + 2; h.fileGrid.select(cursor, DirectoryEntryModel::Replace); frames(h);
        auto cursorInView = [&] {
            for (auto* c : namedAll(h.window, "photoGridCell")) if (c->property("index").toInt() == cursor) {
                const QPointF top = c->mapToItem(view, QPointF(0, 0));
                return top.y() + c->height() > 0 && top.y() < view->height();
            }
            return false;
        };
        QVERIFY(cursorInView());
        details(true); QVERIFY2(cursorInView(), "the cursor left the view in details mode");
        details(false); QVERIFY2(cursorInView(), "the cursor left the view back in thumbnails");
    }

    // N-1: the type chips are reachable from the grid and operable with the keyboard, show focus and have names.
    void typeChipsWorkFromTheKeyboard() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("sub1") && QDir(dir.path()).mkpath("sub2"));
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(dir.filePath("a.png")) && image.save(dir.filePath("b.png")) && image.save(dir.filePath("c.png")));
        for (const char* name : {"notes.txt", "todo.txt"}) { QFile f(dir.filePath(name)); QVERIFY(f.open(QIODevice::WriteOnly)); f.close(); }
        QAccessible::setActive(true);
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.open(dir.filePath("a.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "gridMode"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 7, 10000);
        auto chip = [&](const char* id) { return named(h.window, QString("gridKind-") + id); };
        auto* view = named(h.window, "photoGridView"); QVERIFY(view);
        QTRY_VERIFY_WITH_TIMEOUT(view->hasActiveFocus(), 10000);
        for (const char* id : {"folder", "image", "file"}) { QVERIFY(chip(id)); QTRY_VERIFY_WITH_TIMEOUT(chip(id)->isVisible(), 10000); }
        // Names and roles for assistive technology.
        for (auto [id, name] : {std::pair{"folder", "Show only Folders"}, {"image", "Show only Photos"}, {"file", "Show only Other files"}}) {
            auto* iface = QAccessible::queryAccessibleInterface(chip(id));
            QVERIFY(iface);
            QCOMPARE(iface->text(QAccessible::Name), QString(name));
            QCOMPARE(iface->role(), QAccessible::CheckBox);
        }
        // Ctrl+Shift+F from the grid moves to the chips, and the focus ring is the keyboard one.
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(chip("folder")->hasActiveFocus(), 10000);
        QVERIFY(chip("folder")->property("visualFocus").toBool());
        QVERIFY(!view->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 2, 10000); QCOMPARE(h.fileGrid.kindFilter(), QString("folder"));
        QVERIFY(chip("folder")->hasActiveFocus()); // still there, ready for the next key
        auto* folderIface = QAccessible::queryAccessibleInterface(chip("folder"));
        QVERIFY(folderIface->state().checked);
        // Tab walks to the next chip; Enter switches it.
        QTest::keyClick(h.window, Qt::Key_Tab);
        QTRY_VERIFY_WITH_TIMEOUT(chip("image")->hasActiveFocus(), 10000);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 3, 10000); QCOMPARE(h.fileGrid.kindFilter(), QString("image"));
        // Space again on the active chip clears the type filter.
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 7, 10000); QVERIFY(!h.fileGrid.hasFilter());
        // Escape gives the keyboard back to the items and does not leave the grid.
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(view->hasActiveFocus(), 10000);
        QVERIFY(flag(h, "gridMode"));
        // The shortcut reaches the active chip when a filter is on.
        h.fileGrid.setKindFilter("file"); QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 2, 10000);
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(chip("file")->hasActiveFocus(), 10000);
        // A pointer click on a chip leaves the keyboard on the items.
        auto* photos = chip("image");
        QTest::mouseMove(h.window, sceneCenter(photos)); frames(h);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(photos), 60);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(view->hasActiveFocus(), 10000);
    }

    // R2-N5: Left and Right move between the type chips and stop at the ends; the grid's cursor is not touched.
    void typeChipsMoveWithTheArrowKeys() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("sub1"));
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(dir.filePath("a.png")) && image.save(dir.filePath("b.png")));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.open(dir.filePath("a.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY_WITH_TIMEOUT(flag(h, "gridMode"), 10000); QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.count(), 3, 10000);
        auto chip = [&](const char* id) { return named(h.window, QString("gridKind-") + id); };
        auto* view = named(h.window, "photoGridView"); QTRY_VERIFY_WITH_TIMEOUT(view && view->hasActiveFocus(), 10000);
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(chip("folder")->hasActiveFocus(), 10000);
        QTest::keyClick(h.window, Qt::Key_Left); QTest::qWait(100); // sleep-ok: asserting focus stays put at the first chip
        QVERIFY(chip("folder")->hasActiveFocus()); // stops at the first
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY_WITH_TIMEOUT(chip("image")->hasActiveFocus(), 10000);
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY_WITH_TIMEOUT(chip("file")->hasActiveFocus(), 10000);
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(100); // sleep-ok: asserting focus stays put at the last chip
        QVERIFY(chip("file")->hasActiveFocus()); // and at the last
        QTest::keyClick(h.window, Qt::Key_Left); QTRY_VERIFY_WITH_TIMEOUT(chip("image")->hasActiveFocus(), 10000);
        QVERIFY(chip("image")->property("visualFocus").toBool());
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.kindFilter(), QString("image"), 10000);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(view->hasActiveFocus(), 10000);
        QVERIFY(flag(h, "gridMode"));
        QCOMPARE(h.viewer.fileName(), QString("a.png"));
    }
    // R3-N2: a wheel turn toward the end of the strip never moves it backwards (its clamp includes the margin that reveal() uses).
    void tabStripWheelRespectsTheMargin() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QStringList names{"A fairly long folder name from last summer's trip to the coast with the whole family",
            "Another extremely long folder name that keeps going and going well past any sensible tab width",
            "Third folder with a name that is also much longer than a tab should ever be"};
        QStringList folders; for (const auto& name : names) { folders << makeFolder(root.path(), name, 2); QVERIFY(!folders.last().isEmpty()); }
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(folders[0]).filePath("A-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h));
        for (int i = 1; i < 3; ++i) {
            h.viewer.trigger("tab-new"); QTRY_COMPARE_WITH_TIMEOUT(int(h.viewer.tabs().size()), i + 1, 10000);
            h.viewer.open(QDir(folders[i]).filePath(QString("%1-1.png").arg(names[i].left(1)))); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        }
        auto* strip = named(h.window, "tabStrip"); QVERIFY(strip); QTRY_VERIFY_WITH_TIMEOUT(strip->isVisible(), 10000);
        QMetaObject::invokeMethod(h.window, "wakeHud"); frames(h);
        QTRY_VERIFY_WITH_TIMEOUT(strip->property("contentX").toReal() > 0, 10000); // the active third tab was scrolled into view, into the margin
        const qreal before = strip->property("contentX").toReal();
        const QPoint at = sceneCenter(strip);
        sendWheel(h, at, QPoint(), QPoint(0, -120), Qt::NoModifier); // toward the end
        QTest::qWait(300); // sleep-ok: asserting the strip does not move backwards
        QVERIFY2(strip->property("contentX").toReal() >= before - 0.5, qPrintable(QString("contentX %1 -> %2").arg(before).arg(strip->property("contentX").toReal())));
    }
    // N-5: long tab names elide, the active tab and its buttons stay in view, and New tab never scrolls away.
    void longTabNamesKeepTheActiveTabInView() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QStringList names{
            "A fairly long folder name from last summer's trip to the coast with the whole family",
            "Another extremely long folder name that keeps going and going well past any sensible tab width",
            "Third folder with a name that is also much longer than a tab should ever be"};
        QStringList folders;
        for (const auto& name : names) { folders << makeFolder(root.path(), name, 2); QVERIFY(!folders.last().isEmpty()); }
        const QString grabDir = qEnvironmentVariable("STACK_CONFIRM_FIX_GRAB_DIR");
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(QDir(folders[0]).filePath("A-1.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h));
        for (int i = 1; i < 3; ++i) {
            h.viewer.trigger("tab-new"); QTRY_COMPARE_WITH_TIMEOUT(int(h.viewer.tabs().size()), i + 1, 10000);
            h.viewer.open(QDir(folders[i]).filePath(QString("%1-1.png").arg(names[i].left(1)))); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
            QTRY_COMPARE_WITH_TIMEOUT(h.viewer.activeTab(), i, 10000);
        }
        auto* strip = named(h.window, "tabStrip"); QVERIFY(strip); QTRY_VERIFY_WITH_TIMEOUT(strip->isVisible(), 10000);
        if (!grabDir.isEmpty()) { // evidence first, so a failing run still leaves its pictures
            for (const int width : {1180, 560}) {
                h.window->resize(width, 780); h.viewer.activateTab(2); QTRY_COMPARE_WITH_TIMEOUT(h.viewer.activeTab(), 2, 10000);
                QMetaObject::invokeMethod(h.window, "wakeHud"); frames(h);
                QVERIFY(h.window->grabWindow().save(QDir(grabDir).filePath(QString("tabs-%1.png").arg(width))));
            }
        }
        for (const int width : {1180, 560, 540}) {
            h.window->resize(width, 780); frames(h);
            for (int active : {2, 0, 1, 2}) {
                h.viewer.activateTab(active);
                QTRY_COMPARE_WITH_TIMEOUT(h.viewer.activeTab(), active, 10000);
                QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 10000);
                QMetaObject::invokeMethod(h.window, "wakeHud"); frames(h); // the bars auto-hide in photo mode; measure them shown
                auto* tab = named(h.window, QString("tab-%1").arg(active));
                auto* close = named(h.window, QString("tabClose-%1").arg(active));
                auto* add = named(h.window, "tabNew");
                QVERIFY(tab && close && add);
                const QString where = QString("width %1, active tab %2").arg(width).arg(active);
                QVERIFY2(fullyInside(strip, tab), qPrintable(where + ": the active tab is out of view"));
                QVERIFY2(fullyInside(strip, close), qPrintable(where + ": its close button is out of view"));
                const QRectF addScene = add->mapRectToScene(QRectF(0, 0, add->width(), add->height()));
                QVERIFY2(addScene.left() >= 0 && addScene.right() <= width, qPrintable(where + QString(": New tab is out of view (x %1..%2)").arg(addScene.left()).arg(addScene.right())));
                QVERIFY2(tab->width() <= strip->property("tabCap").toReal() + 0.5, qPrintable(where + ": a tab is wider than the cap"));
                // R2-N3: no tab is left as a clipped sliver at the strip's left edge.
                for (int i = 0; i < 3; ++i) for (const char* prefix : {"tab-", "tabClose-"}) {
                    auto* part = named(h.window, QString("%1%2").arg(prefix).arg(i));
                    const QRectF r = inStrip(strip, part);
                    QVERIFY2(!(r.left() < -0.5 && r.right() > 0.5), qPrintable(where + QString(": %1%2 is clipped at the left edge (x %3..%4)").arg(prefix).arg(i).arg(r.left()).arg(r.right())));
                }
            }
            // A long name is elided, not allowed to take its natural width.
            auto* tab0 = named(h.window, "tab-0");
            QVERIFY(tab0->width() < tab0->property("implicitWidth").toReal());

        }
    }
};

QTEST_MAIN(StackConfirmUiTests)
#include "stack_confirm_ui_tests.moc"
