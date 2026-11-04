// STACK-CONFIRM-FIX: regressions found by the whole-stack confirmation review (docs/verification/STACK-CONFIRM-REVIEW.md).
// (A vanished-file slideshow case was tried and dropped: the viewer relists at once, so the vanished name never reaches a decode;
//  the undecodable-photo case exercises the same fail() path and is the one that fails on the base.)
//   B-1 slideshow over undecodable or vanished photos, N-2 slideshow ends on tab and folder navigation, G-1 details mode keeps the
//   visible anchor, N-1 type chips on the keyboard, N-5 long tab names. One executable, offscreen, like the other *_ui tests.
#include "ui_test_support.hpp"
#include <QAccessible>
#include <QAccessibleInterface>
#include <QSharedMemory>
#include <QUuid>
#include <atomic>
#include <thread>
#include "../src/app/shared_memory_reaper.hpp"

namespace {
QQuickItem* noticeItem(Harness& h) { return named(h.window, "deleteNotice"); }
QString noticeText(Harness& h) { auto* item = noticeItem(h); return item ? item->property("text").toString() : QString(); }
void setSlideshowSeconds(Harness& h, int seconds) {
    QVERIFY(QMetaObject::invokeMethod(h.window, "setSlideshowSeconds", Q_ARG(QVariant, seconds)));
}
// Ends a slideshow and lets any step it had in flight land before the harness and its folders are torn down.
// Lets the window render a few frames (layout and polish settle) by waiting on a condition, never on a fixed sleep.
void frames(Harness& h, int count = 3) {
    int seen = 0;
    const auto c1 = QObject::connect(h.window, &QQuickWindow::frameSwapped, h.window, [&] { ++seen; });
    const auto c2 = QObject::connect(h.window, &QQuickWindow::afterAnimating, h.window, [&] { ++seen; });
    const auto c3 = QObject::connect(h.window, &QQuickWindow::beforeSynchronizing, h.window, [&] { ++seen; });
    QTRY_VERIFY_WITH_TIMEOUT([&] { h.window->requestUpdate(); return seen >= count; }(), 10000);
    QObject::disconnect(c1); QObject::disconnect(c2); QObject::disconnect(c3);
}
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
        QTRY_COMPARE(h.viewer.currentIndex(), 1);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY(flag(h, "slideshowOn"));
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
        QTRY_COMPARE(h.viewer.currentIndex(), 1);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 1);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY(flag(h, "slideshowOn"));
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
        QTRY_VERIFY(flag(h, "slideshowOn"));
        QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()).endsWith("/b"), 10000);
        QTest::qWait(300); // sleep-ok: a closing of an inactive tab must not end it: asserting nothing fires
        QVERIFY2(flag(h, "slideshowOn"), "the slideshow's own step into the next folder ended it");
        settleSlideshow(h);
    }
    // R2-N1: a failed photo is passed after about a second even at a slow pace (it is not held for a whole interval).
    void slideshowPassesABadPhotoPromptlyAtASlowPace() {
        Media media(3, QSize(64, 48));
        overwriteWithText(media.path(1));
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        QTRY_COMPARE(h.viewer.currentIndex(), 1);
        h.viewer.setBoundaryMode("loop");
        setSlideshowSeconds(h, 6);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY(flag(h, "slideshowOn"));
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
        QTRY_VERIFY(flag(h, "slideshowOn"));
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.fileName(), QString("c-2.png"), 15000);
        QVERIFY2(flag(h, "slideshowOn"), "the slideshow stopped on its way through the one-photo folder");
        QVERIFY2(noticeText(h).isEmpty(), qPrintable(noticeText(h)));
        settleSlideshow(h);
    }
    // R2-N2: closing a tab other than the active one is not navigation.
    void slideshowSurvivesClosingAnotherTab() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        h.viewer.trigger("tab-new"); QTRY_COMPARE(int(h.viewer.tabs().size()), 2);
        QTRY_COMPARE(h.viewer.activeTab(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 10000);
        setSlideshowSeconds(h, 5);
        h.viewer.trigger("slideshow");
        QTRY_VERIFY(flag(h, "slideshowOn"));
        QVERIFY(h.viewer.closeTab(0)); // the active tab becomes number 0
        QTRY_COMPARE(h.viewer.activeTab(), 0);
        frames(h);
        QVERIFY2(flag(h, "slideshowOn"), "closing an inactive tab ended the slideshow");
        settleSlideshow(h);
    }
    // R3-B2: the mechanism of the old permanent freeze, reproduced deterministically. A worker killed while it holds the segment's
    // system semaphore never gives it back; "holder" stands for that dead worker (it locks and never unlocks until the test
    // does). Destroying an attached segment, as the viewer used to do on the GUI thread after killing a worker, then blocks.
    void destroyingASegmentWaitsForTheLockOfADeadWorker() {
        const QString key = "Glancelot-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto* segment = new QSharedMemory(key);
        QVERIFY(segment->create(4096));
        QSharedMemory holder(key);
        QVERIFY(holder.attach());
        QVERIFY(holder.lock()); // the "dead worker": holds the semaphore
        std::atomic<bool> done{false};
        std::thread destroyer([&] { delete segment; done = true; });
        QTest::qWait(700); // sleep-ok: asserting the destructor stays blocked while a dead worker holds the lock
        QVERIFY2(!done.load(), "the destructor returned although a dead worker holds the lock: the old hang is not reproduced");
        QVERIFY(holder.unlock());
        destroyer.join();
        QVERIFY(done.load());
    }
    // R3-B2: with the reaper the GUI thread hands the segment off and never waits, even while the lock is held.
    // (Fail-before: the code it replaces is exactly the blocking call in the test above.)
    void theReaperNeverBlocksTheCallerOnAHeldLock() {
        namespace d = glancelot::detail;
        const QString key = "Glancelot-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        d::TransferPtr segment = d::makeTransfer(key);
        QVERIFY(segment->create(4096));
        QSharedMemory holder(key);
        QVERIFY(holder.attach());
        QVERIFY(holder.lock());
        int heartbeats = 0;
        QTimer beat; QObject::connect(&beat, &QTimer::timeout, [&] { ++heartbeats; }); beat.start(20);
        QElapsedTimer elapsed; elapsed.start();
        segment.reset(); // what cancel() does after killing the worker
        QVERIFY2(elapsed.elapsed() < 150, qPrintable(QString::number(elapsed.elapsed())));
        QCOMPARE(d::pendingSharedMemoryReaps().load(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= 10, 10000);
        QVERIFY(heartbeats >= 10); // the event loop kept running while the teardown waited
        QCOMPARE(d::pendingSharedMemoryReaps().load(), 1); // still waiting on the dead worker, off the GUI thread
        QVERIFY(holder.unlock());
        QTRY_COMPARE_WITH_TIMEOUT(d::pendingSharedMemoryReaps().load(), 0, 10000);
    }
    // R3-B2: the number of teardown threads stuck on dead workers is capped; past the cap the segment is leaked, not waited for.
    void blockedTeardownsAreBounded() {
        namespace d = glancelot::detail;
        std::vector<std::unique_ptr<QSharedMemory>> holders;
        const int total = d::MaxPendingSharedMemoryReaps + 3;
        const int leakedBefore = d::leakedSharedMemorySegments().load();
        for (int i = 0; i < total; ++i) {
            const QString key = "Glancelot-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            d::TransferPtr segment = d::makeTransfer(key);
            QVERIFY(segment->create(1024));
            holders.push_back(std::make_unique<QSharedMemory>(key));
            QVERIFY(holders.back()->attach()); QVERIFY(holders.back()->lock());
            segment.reset();
        }
        QCOMPARE(d::pendingSharedMemoryReaps().load(), d::MaxPendingSharedMemoryReaps);
        QCOMPARE(d::leakedSharedMemorySegments().load() - leakedBefore, 3);
        for (auto& holder : holders) QVERIFY(holder->unlock());
        QTRY_COMPARE_WITH_TIMEOUT(d::pendingSharedMemoryReaps().load(), 0, 10000);
    }
    // R3-B1: `navigating` is true from the request of a step until it has settled, and false again afterwards.
    void navigatingSpansAStep() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QTRY_VERIFY(!h.viewer.navigating());
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
        QTRY_VERIFY(flag(h, "slideshowOn"));
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
        QTRY_VERIFY(flag(h, "slideshowOn"));
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
        invoke("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn")); QTRY_VERIFY(indicatorShown());
        invoke("tab-new");
        QTRY_VERIFY(!flag(h, "slideshowOn")); QTRY_VERIFY(!indicatorShown());
        // Opening another folder in the same tab (tree, breadcrumbs and the path field all end here).
        h.viewer.openFolder(folder);
        QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()) == QDir::fromNativeSeparators(folder) && h.viewer.imageReady(), 10000);
        invoke("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn"));
        h.viewer.openFolder(media.dir.path()); QTRY_VERIFY_WITH_TIMEOUT(QDir::fromNativeSeparators(h.viewer.directory()) == QDir::fromNativeSeparators(media.dir.path()), 10000);
        QTRY_VERIFY(!flag(h, "slideshowOn")); QTRY_VERIFY(!indicatorShown());
        // Switching back to the first tab ends it too.
        QTRY_VERIFY(h.viewer.imageReady());
        invoke("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn"));
        invoke("tab-previous");
        QTRY_VERIFY(!flag(h, "slideshowOn")); QTRY_VERIFY(!indicatorShown());
    }

    // G-1: changing the grid mode keeps the item that was first in view in view, at any column count.
    void detailsToggleKeepsTheVisibleAnchor() {
        Media media(120, QSize(32, 24)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 120);
        auto* view = named(h.window, "photoGridView"); QVERIFY(view);
        QObject* grid = h.object("photoGrid"); QVERIFY(grid);
        auto firstVisible = [&] {
            int index = -1;
            QMetaObject::invokeMethod(view, "indexAt", Q_RETURN_ARG(int, index), Q_ARG(qreal, 8), Q_ARG(qreal, view->property("contentY").toReal() + 4));
            return index;
        };
        auto details = [&](bool on) {
            h.viewer.trigger("view-details");
            QTRY_COMPARE(grid->property("details").toBool(), on);
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
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.fileGrid.count(), 7);
        auto chip = [&](const char* id) { return named(h.window, QString("gridKind-") + id); };
        auto* view = named(h.window, "photoGridView"); QVERIFY(view);
        QTRY_VERIFY(view->hasActiveFocus());
        for (const char* id : {"folder", "image", "file"}) { QVERIFY(chip(id)); QTRY_VERIFY(chip(id)->isVisible()); }
        // Names and roles for assistive technology.
        for (auto [id, name] : {std::pair{"folder", "Show only Folders"}, {"image", "Show only Photos"}, {"file", "Show only Other files"}}) {
            auto* iface = QAccessible::queryAccessibleInterface(chip(id));
            QVERIFY(iface);
            QCOMPARE(iface->text(QAccessible::Name), QString(name));
            QCOMPARE(iface->role(), QAccessible::CheckBox);
        }
        // Ctrl+Shift+F from the grid moves to the chips, and the focus ring is the keyboard one.
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(chip("folder")->hasActiveFocus());
        QVERIFY(chip("folder")->property("visualFocus").toBool());
        QVERIFY(!view->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE(h.fileGrid.count(), 2); QCOMPARE(h.fileGrid.kindFilter(), QString("folder"));
        QVERIFY(chip("folder")->hasActiveFocus()); // still there, ready for the next key
        auto* folderIface = QAccessible::queryAccessibleInterface(chip("folder"));
        QVERIFY(folderIface->state().checked);
        // Tab walks to the next chip; Enter switches it.
        QTest::keyClick(h.window, Qt::Key_Tab);
        QTRY_VERIFY(chip("image")->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE(h.fileGrid.count(), 3); QCOMPARE(h.fileGrid.kindFilter(), QString("image"));
        // Space again on the active chip clears the type filter.
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE(h.fileGrid.count(), 7); QVERIFY(!h.fileGrid.hasFilter());
        // Escape gives the keyboard back to the items and does not leave the grid.
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(view->hasActiveFocus());
        QVERIFY(flag(h, "gridMode"));
        // The shortcut reaches the active chip when a filter is on.
        h.fileGrid.setKindFilter("file"); QTRY_COMPARE(h.fileGrid.count(), 2);
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(chip("file")->hasActiveFocus());
        // A pointer click on a chip leaves the keyboard on the items.
        auto* photos = chip("image");
        QTest::mouseMove(h.window, sceneCenter(photos)); frames(h);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(photos), 60);
        QTRY_COMPARE(h.fileGrid.count(), 3);
        QTRY_VERIFY(view->hasActiveFocus());
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
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 3);
        auto chip = [&](const char* id) { return named(h.window, QString("gridKind-") + id); };
        auto* view = named(h.window, "photoGridView"); QTRY_VERIFY(view && view->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(chip("folder")->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Left); QTest::qWait(100); // sleep-ok: asserting focus stays put at the first chip
        QVERIFY(chip("folder")->hasActiveFocus()); // stops at the first
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY(chip("image")->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY(chip("file")->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(100); // sleep-ok: asserting focus stays put at the last chip
        QVERIFY(chip("file")->hasActiveFocus()); // and at the last
        QTest::keyClick(h.window, Qt::Key_Left); QTRY_VERIFY(chip("image")->hasActiveFocus());
        QVERIFY(chip("image")->property("visualFocus").toBool());
        QTest::keyClick(h.window, Qt::Key_Space);
        QTRY_COMPARE(h.fileGrid.kindFilter(), QString("image"));
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(view->hasActiveFocus());
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
            h.viewer.trigger("tab-new"); QTRY_COMPARE(int(h.viewer.tabs().size()), i + 1);
            h.viewer.open(QDir(folders[i]).filePath(QString("%1-1.png").arg(names[i].left(1)))); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        }
        auto* strip = named(h.window, "tabStrip"); QVERIFY(strip); QTRY_VERIFY(strip->isVisible());
        QMetaObject::invokeMethod(h.window, "wakeHud"); frames(h);
        QTRY_VERIFY(strip->property("contentX").toReal() > 0); // the active third tab was scrolled into view, into the margin
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
            h.viewer.trigger("tab-new"); QTRY_COMPARE(int(h.viewer.tabs().size()), i + 1);
            h.viewer.open(QDir(folders[i]).filePath(QString("%1-1.png").arg(names[i].left(1)))); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
            QTRY_COMPARE(h.viewer.activeTab(), i);
        }
        auto* strip = named(h.window, "tabStrip"); QVERIFY(strip); QTRY_VERIFY(strip->isVisible());
        if (!grabDir.isEmpty()) { // evidence first, so a failing run still leaves its pictures
            for (const int width : {1180, 560}) {
                h.window->resize(width, 780); h.viewer.activateTab(2); QTRY_COMPARE(h.viewer.activeTab(), 2);
                QMetaObject::invokeMethod(h.window, "wakeHud"); frames(h);
                QVERIFY(h.window->grabWindow().save(QDir(grabDir).filePath(QString("tabs-%1.png").arg(width))));
            }
        }
        for (const int width : {1180, 560, 540}) {
            h.window->resize(width, 780); frames(h);
            for (int active : {2, 0, 1, 2}) {
                h.viewer.activateTab(active);
                QTRY_COMPARE(h.viewer.activeTab(), active);
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
