// STACK-F040-PINNED: the F040 reference window and the MONITOR-OUTPUT pinned output are distinct commands that share
// the display manager's aggregate retained-image budget. Parity cases: close, pin replacement, recovery while both are
// active, and the budget's refusal/shedding paths. Every case checks that the selection never changes.
#include "ui_test_support.hpp"
#include "glancelot/display_manager.hpp"
#include "presentation_window.hpp"
#include <QSet>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <atomic>

namespace {
struct BudgetScope {
    qint64 saved{glancelot::DisplayManager::imageBudgetBytes()};
    ~BudgetScope() { glancelot::DisplayManager::setImageBudgetBytes(saved); }
};
struct Selection {
    int index; QString path; qint64 key;
    explicit Selection(const ViewerController& viewer)
        : index(viewer.currentIndex()), path(viewer.filePath()), key(viewer.image().cacheKey()) {}
    bool same(const ViewerController& viewer) const {
        return viewer.currentIndex() == index && viewer.filePath() == path && viewer.image().cacheKey() == key;
    }
};
void flushDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); }
QString labelOf(const ViewerController& viewer, const QString& id) {
    for (const auto& row : viewer.commands()) if (row.toMap().value("id") == id) return row.toMap().value("label").toString();
    return {};
}
constexpr const char* kOpenRefused = "Not enough image memory to open the reference window.";
constexpr const char* kReplaceRefused = "Not enough image memory to replace the reference photo. The current one stays.";
constexpr const char* kTooLarge = "This photo is too large for the image memory budget.";
constexpr const char* kUnpin = "Unpin the output photo to make room.";
constexpr const char* kCloseComparison = "Close the comparison on the output to make room.";
constexpr const char* kReleaseHeld = "Turn on Follow controller selection to release the output's held photo.";
QString joined(const char* a, const char* b) { return QString::fromUtf8(a) + u' ' + QString::fromUtf8(b); }
} // namespace

class ReferencePinTests : public QObject {
    Q_OBJECT
    static bool step(Harness& h, int index) {
        h.viewer.trigger("next");
        return QTest::qWaitFor([&] { return h.viewer.currentIndex() == index && h.viewer.imageReady(); }, 15000);
    }
    static QQuickWindow* referenceWindow(Harness& h) { return h.window->findChild<QQuickWindow*>("referenceWindow"); }
    static ImageCanvas* referenceCanvas(Harness& h) { return h.window->findChild<ImageCanvas*>("referenceCanvas"); }
    // Pins the current photo both ways: the reference window and the presentation output's pinned mode.
    static bool pinBoth(Harness& h) {
        h.viewer.trigger("reference-pin");
        if (!h.viewer.displayManager()->isPresentationActive()) h.viewer.trigger("presentation-toggle");
        h.viewer.trigger("presentation-pin");
        return QTest::qWaitFor([&] { return referenceWindow(h)->isVisible(); }, 5000)
            && h.viewer.presentationMode() == "pinned";
    }
    static bool nativeRhi(Harness& h) {
        const auto* renderer = h.window->rendererInterface();
        return renderer && QSGRendererInterface::isApiRhiBased(renderer->graphicsApi());
    }
    static QString notice(Harness& h) { return h.object("deleteFlow")->property("noticeText").toString(); }
    // Records the lowest admitted texture byte count the reference canvas reports after any frame (render thread).
    struct AdmissionFloor {
        std::atomic<qint64> lowest{0};
        QMetaObject::Connection connection;
        AdmissionFloor(QQuickWindow* window, ImageCanvas* canvas) {
            connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [this, canvas] {
                const qint64 now = canvas->admittedTextureBytes();
                qint64 seen = lowest.load();
                while (now < seen && !lowest.compare_exchange_weak(seen, now)) {}
            }, Qt::DirectConnection);
        }
        ~AdmissionFloor() { QObject::disconnect(connection); }
    };

    QTemporaryDir* config{};
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
        glancelot::registerPresentationWindowFactory();
    }
    // Every case gets its own preferences; no presentation window outlives its harness.
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { flushDeletes(); qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr; }

    void commandsStayDistinct() {
        Harness h; QVERIFY(h.window);
        for (const auto* id : {"reference-pin", "reference-close", "presentation-pin", "presentation-close"})
            QVERIFY2(!labelOf(h.viewer, id).isEmpty(), id);
        QVERIFY(labelOf(h.viewer, "reference-pin") != labelOf(h.viewer, "presentation-pin"));
        QVERIFY(labelOf(h.viewer, "reference-close") != labelOf(h.viewer, "presentation-close"));
        // Failure case: nothing decoded, so neither pin takes hold and no window opens.
        QSignalSpy changed(&h.viewer, &ViewerController::referenceChanged);
        QVERIFY(!h.viewer.pinReference());
        h.viewer.trigger("reference-pin"); QTest::qWait(50);
        QCOMPARE(notice(h), QString("Open a photo first to pin it as a reference."));
        QVERIFY(h.viewer.referenceName().isEmpty());
        QVERIFY(!referenceWindow(h)->isVisible());
        QCOMPARE(changed.count(), 0);
        QVERIFY(h.viewer.displayManager()->pinnedPath().isEmpty());
    }

    void bothPinsShareOneDecodedFrame() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        const Selection selection(h.viewer);
        QVERIFY(pinBoth(h));
        QVERIFY(selection.same(h.viewer));
        // No second decode: the reference, the output pin and the canvas all hold the controller's frame.
        QCOMPARE(h.viewer.referenceImage().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->referenceImage().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QCOMPARE(dm->referencePath(), media.path(0));
        QCOMPARE(referenceCanvas(h)->image().cacheKey(), h.viewer.image().cacheKey());
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QCOMPARE(output->primaryCanvas()->image().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->retainedImageBytes(), one);   // counted once
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void closeParity() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        QVERIFY(pinBoth(h));
        QVERIFY(step(h, 2));
        const Selection selection(h.viewer);
        QCOMPARE(dm->retainedImageBytes(), 2 * one);   // p2 current; p1 shared by both pins and the output's history

        // Closing the reference leaves the output pin and the selection alone, and releases its pixels.
        h.viewer.trigger("reference-close");
        QTRY_VERIFY(!referenceWindow(h)->isVisible());
        QVERIFY(h.viewer.referenceName().isEmpty() && dm->referenceImage().isNull());
        QVERIFY(referenceCanvas(h)->image().isNull());
        QCOMPARE(h.viewer.presentationMode(), QString("pinned"));
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QVERIFY(selection.same(h.viewer));

        // Closing the output leaves a re-pinned reference alone; the output pin's pixels stay accounted.
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        h.viewer.trigger("presentation-close"); flushDeletes();
        QVERIFY(!dm->isPresentationActive());
        QVERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(h.viewer.referenceImage().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->retainedImageBytes(), 2 * one);   // p2 (current + reference) and the retained output pin p1
        QVERIFY(selection.same(h.viewer));

        // Unpinning the output leaves the reference; the window's own close button clears only the reference.
        h.viewer.unpinPresentationImage();
        QVERIFY(dm->pinnedPath().isEmpty());
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        QCOMPARE(dm->retainedImageBytes(), one);
        referenceWindow(h)->close();
        QTRY_VERIFY(h.viewer.referenceName().isEmpty());
        QCOMPARE(dm->retainedImageBytes(), one);
        QVERIFY(selection.same(h.viewer));
    }

    void pinReplacementParity() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        // Distinct frames: reference p1, output pin p2, current p3. A leaked old reference would then cost a third
        // image instead of hiding behind the output pin.
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());
        const QImage first = h.viewer.image();
        QVERIFY(step(h, 2));
        h.viewer.trigger("presentation-toggle"); h.viewer.trigger("presentation-pin");
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QVERIFY(step(h, 3));
        QCOMPARE(dm->retainedImageBytes(), 3 * one);   // p3 current, p2 pin (and history), p1 reference
        const Selection selection(h.viewer);
        QSignalSpy pinned(dm, &glancelot::DisplayManager::pinnedPathChanged);

        // Replacing the reference does not touch the output pin, and the old reference frame is released.
        h.viewer.trigger("reference-pin");
        QCOMPARE(h.viewer.referenceName(), QString("p3.png"));
        QCOMPARE(referenceCanvas(h)->image().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(pinned.count(), 0);
        QCOMPARE(dm->retainedImageBytes(), 2 * one);   // p3 (current + reference) and p2; p1 is gone
        QVERIFY(selection.same(h.viewer));

        // Replacing the output pin does not touch the reference.
        QSignalSpy reference(&h.viewer, &ViewerController::referenceChanged);
        h.viewer.trigger("presentation-pin");
        QCOMPARE(dm->pinnedPath(), media.path(2));
        QCOMPARE(reference.count(), 0);
        QCOMPARE(h.viewer.referenceName(), QString("p3.png"));
        QVERIFY(selection.same(h.viewer));

        // Stale requests from an older selection generation are refused by both pins.
        QVERIFY(!dm->setReferenceImage(media.path(0), first, 0));
        dm->pinImage(media.path(0), first, 0);
        QCOMPARE(h.viewer.referenceName(), QString("p3.png"));
        QCOMPARE(dm->pinnedPath(), media.path(2));
        QCOMPARE(reference.count(), 0);
        QVERIFY(selection.same(h.viewer));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void recoveryWhileBothActive() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        // Distinct pins (reference p1, output p2) so the output canvas can only match its own pin.
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());
        const qint64 referenceKey = h.viewer.image().cacheKey();
        QVERIFY(step(h, 2));
        h.viewer.trigger("presentation-toggle"); h.viewer.trigger("presentation-pin");
        QCOMPARE(dm->pinnedPath(), media.path(1));
        const qint64 pinnedKey = h.viewer.image().cacheKey();
        QVERIFY(pinnedKey != referenceKey);
        QVERIFY(step(h, 3));
        const Selection selection(h.viewer);

        // Low-memory release drops optional history only; both explicit pins survive.
        dm->releaseCachedImages();
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(dm->retainedImageBytes(), 3 * one);   // p3 current, p2 pin, p1 reference

        // Emergency recovery returns everything to the controller: the output closes, the reference window stays.
        h.viewer.trigger("monitor-recover"); flushDeletes();
        QTRY_VERIFY(!dm->isPresentationActive());
        QVERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QCOMPARE(dm->referenceImage().cacheKey(), referenceKey);
        QCOMPARE(referenceCanvas(h)->image().cacheKey(), referenceKey);
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(dm->retainedImageBytes(), 3 * one);
        QVERIFY(selection.same(h.viewer));

        // Reopening the output after recovery shows its own pin again; the reference is unchanged.
        QVERIFY(h.viewer.displayManager()->openPresentation({}, glancelot::PresentationMode::Pinned));
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QCOMPARE(output->primaryCanvas()->image().cacheKey(), pinnedKey);
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QVERIFY(selection.same(h.viewer));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void budgetShedsReferenceBeforeOutputPin() {
        BudgetScope budget;
        Media media(4, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        glancelot::DisplayManager::setImageBudgetBytes(2 * one + 16);
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());   // p1
        QVERIFY(step(h, 2));
        h.viewer.trigger("presentation-toggle"); h.viewer.trigger("presentation-pin");      // p2
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(dm->retainedImageBytes(), 2 * one);
        QSignalSpy shed(dm, &glancelot::DisplayManager::referenceShed);

        // A newer current frame is always admitted: the reference goes first, the audience-facing pin stays.
        QVERIFY(step(h, 3));
        QCOMPARE(shed.count(), 1);
        QCOMPARE(shed.at(0).at(0).toString(), media.path(0));
        QTRY_VERIFY(!referenceWindow(h)->isVisible());
        QVERIFY(h.viewer.referenceName().isEmpty());
        QVERIFY(h.viewer.status().contains("p1.png"));
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(h.viewer.presentationMode(), QString("pinned"));
        QCOMPARE(h.viewer.filePath(), media.path(2));
        QVERIFY(dm->retainedImageBytes() <= 2 * one + 16);

        // Re-pinning the current frame costs nothing extra, so it is admitted.
        QVERIFY(h.viewer.pinReference());
        QTRY_VERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(dm->retainedImageBytes(), 2 * one);

        // Failure: a frame that cannot fit beside the current and pinned frames is refused; the old reference stays.
        const Selection selection(h.viewer);
        QImage extra(h.viewer.image().size(), QImage::Format_RGBA8888); extra.fill(Qt::magenta);
        QVERIFY(extra.sizeInBytes() >= one);
        QSignalSpy changed(&h.viewer, &ViewerController::referenceChanged);
        QVERIFY(!dm->setReferenceImage(media.path(3), extra, quint64(1) << 40));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(h.viewer.referenceName(), QString("p3.png"));
        QVERIFY(dm->retainedImageBytes() <= 2 * one + 16);

        // An output pin that cannot fit never evicts the reference; the pin itself is shed as before.
        QImage large(h.viewer.image().width(), 2 * h.viewer.image().height(), QImage::Format_RGBA8888); large.fill(Qt::yellow);
        dm->pinImage(media.path(3), large, quint64(1) << 40);
        QVERIFY(dm->pinnedPath().isEmpty());
        QCOMPARE(h.viewer.presentationMode(), QString("mirror"));
        QCOMPARE(h.viewer.referenceName(), QString("p3.png"));
        QCOMPARE(changed.count(), 0);
        QVERIFY(dm->retainedImageBytes() <= 2 * one + 16);
        QVERIFY(selection.same(h.viewer));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    // Review round 1: a reference pin never sheds the output's held (pending) frame or changes its status; it is
    // refused instead.
    void referenceNeverShedsOutputHeldFrame() {
        BudgetScope budget;
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        h.viewer.trigger("presentation-toggle"); QVERIFY(dm->isPresentationActive());
        dm->setRoutingPolicy({{"followSelection", false}});
        QVERIFY(step(h, 2));   // the output keeps p1; p2 waits as its held latest frame
        QCOMPARE(dm->retainedImageBytes(), 2 * one);
        glancelot::DisplayManager::setImageBudgetBytes(2 * one + 16);
        const QString status = dm->routingStatus();
        const Selection selection(h.viewer);
        QSignalSpy routing(dm, &glancelot::DisplayManager::routingStatusChanged);
        QSignalSpy changed(&h.viewer, &ViewerController::referenceChanged);

        // Fits beside the current frame alone, not beside the held frame too: refused, nothing changes.
        QImage extra(h.viewer.image().size(), QImage::Format_RGBA8888); extra.fill(Qt::magenta);
        QVERIFY(!dm->setReferenceImage(media.path(2), extra, quint64(1) << 40));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(routing.count(), 0);
        QCOMPARE(dm->routingStatus(), status);
        QCOMPARE(dm->retainedImageBytes(), 2 * one);
        QVERIFY(h.viewer.referenceName().isEmpty());
        QVERIFY(selection.same(h.viewer));

        // Resuming still delivers the held frame, so it was never shed.
        dm->setRoutingPolicy({{"followSelection", true}});
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QTRY_COMPARE(output->primaryCanvas()->image().cacheKey(), h.viewer.image().cacheKey());
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    // Review rounds 1-2: a budget refusal has its own notice. It says whether the window did not open or the old
    // reference stays, and names the next step for what is actually holding the memory.

    void budgetRefusalHasItsOwnNotice() {   // open refused; the photo alone is too large
        BudgetScope budget;
        Media media(2, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        glancelot::DisplayManager::setImageBudgetBytes(h.viewer.image().sizeInBytes() / 2);
        const Selection selection(h.viewer);
        h.viewer.trigger("reference-pin"); QTest::qWait(50);
        QVERIFY(h.viewer.referenceName().isEmpty());
        QVERIFY(!referenceWindow(h)->isVisible());
        QCOMPARE(notice(h), joined(kOpenRefused, kTooLarge));
        QVERIFY(selection.same(h.viewer));
    }

    void refusalNoticeNamesThePinnedOutput() {   // open refused; the output's pinned photo holds the memory
        BudgetScope budget;
        Media media(2, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        const qint64 one = h.viewer.image().sizeInBytes();
        h.viewer.trigger("presentation-toggle"); h.viewer.trigger("presentation-pin");   // output pin p1
        QVERIFY(step(h, 2));
        glancelot::DisplayManager::setImageBudgetBytes(2 * one - 16);
        h.viewer.trigger("reference-pin"); QTest::qWait(50);
        QVERIFY(!referenceWindow(h)->isVisible());
        QCOMPARE(notice(h), joined(kOpenRefused, kUnpin));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void refusalNoticeNamesTheComparison() {   // open refused; the output's comparison photo holds the memory
        BudgetScope budget;
        Media media(2, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        h.viewer.trigger("presentation-toggle"); QVERIFY(dm->isPresentationActive());
        QVERIFY(step(h, 2));
        QImage other(h.viewer.image().size(), QImage::Format_RGBA8888); other.fill(Qt::cyan);
        dm->setComparisonImage("comparison", other, quint64(1) << 40);
        QCOMPARE(h.viewer.presentationMode(), QString("comparison"));
        glancelot::DisplayManager::setImageBudgetBytes(2 * one - 16);
        h.viewer.trigger("reference-pin"); QTest::qWait(50);
        QVERIFY(!referenceWindow(h)->isVisible());
        QCOMPARE(notice(h), joined(kOpenRefused, kCloseComparison));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void refusalNoticeForReplacementNamesTheHeldFrame() {   // replace refused; an off-screen held frame is the cause
        BudgetScope budget;
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());   // reference p1
        h.viewer.trigger("presentation-toggle"); QVERIFY(dm->isPresentationActive());
        dm->setRoutingPolicy({{"followSelection", false}});
        glancelot::DisplayManager::setImageBudgetBytes(one + 16);
        QVERIFY(step(h, 2));   // the output holds p1 (also the reference), so the latest frame cannot wait beside it
        QCOMPARE(dm->currentPath(), media.path(0));
        h.viewer.trigger("reference-pin"); QTest::qWait(50);
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));   // the old reference stays
        QVERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(notice(h), joined(kReplaceRefused, kReleaseHeld));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    // Review round 2 (LOW-2): a new frame never costs the output its held frame. Speculative caches go first, then the
    // reference window's photo (with a notice that says how to reopen it).
    void heldOutputKeepsItsFrameReferenceIsShed() {
        BudgetScope budget;
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        const qint64 heldKey = h.viewer.image().cacheKey();
        h.viewer.trigger("presentation-toggle"); QVERIFY(dm->isPresentationActive());
        dm->setRoutingPolicy({{"followSelection", false}});
        QVERIFY(step(h, 2));   // output holds p1; p2 waits
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(referenceWindow(h)->isVisible());   // reference p2
        glancelot::DisplayManager::setImageBudgetBytes(2 * one + 16);
        const QString status = dm->routingStatus();
        QSignalSpy shed(dm, &glancelot::DisplayManager::referenceShed);

        QVERIFY(step(h, 3));   // p3 replaces p2 as the waiting frame; only the reference can make room
        QCOMPARE(shed.count(), 1);
        QCOMPARE(shed.at(0).at(0).toString(), media.path(1));
        QTRY_VERIFY(!referenceWindow(h)->isVisible());
        QVERIFY(h.viewer.referenceName().isEmpty());
        QVERIFY(referenceCanvas(h)->image().isNull());
        const QString shedNotice = "Reference window closed to free image memory. To reopen it, open p2.png and pin it again.";
        QCOMPARE(notice(h), shedNotice);
        // The held frame and the waiting frame are intact, the output status is unchanged, and the bytes add up.
        QCOMPARE(dm->currentPath(), media.path(0));
        QCOMPARE(dm->routingStatus(), status);
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QCOMPARE(output->primaryCanvas()->image().cacheKey(), heldKey);
        QCOMPARE(dm->retainedImageBytes(), 2 * one);   // held p1 + waiting p3
        dm->setRoutingPolicy({{"followSelection", true}});
        QTRY_COMPARE(output->primaryCanvas()->image().cacheKey(), h.viewer.image().cacheKey());
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    // Review round 2 (LOW-2): a pinned output photo is never dropped for a newer main frame. The output does not retain
    // the newer frame until the pin is released, and says so.
    void pinnedOutputPhotoIsNeverDropped() {
        BudgetScope budget;
        Media media(2, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        const qint64 pinnedKey = h.viewer.image().cacheKey();
        h.viewer.trigger("presentation-toggle"); h.viewer.trigger("presentation-pin");
        QCOMPARE(h.viewer.presentationMode(), QString("pinned"));
        glancelot::DisplayManager::setImageBudgetBytes(one + 16);
        QVERIFY(step(h, 2));
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QCOMPARE(h.viewer.presentationMode(), QString("pinned"));
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QCOMPARE(output->primaryCanvas()->image().cacheKey(), pinnedKey);
        QCOMPARE(dm->retainedImageBytes(), one);
        QCOMPARE(dm->routingStatus(), QString("Output keeps its pinned photo: the latest frame exceeds the retained-image budget."));
        // Releasing the pin brings the output back to the main photo.
        h.viewer.unpinPresentationImage();
        QTRY_COMPARE(output->primaryCanvas()->image().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->currentPath(), media.path(1));
        QCOMPARE(dm->retainedImageBytes(), one);
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    // Review round 1, native RHI only: closing or shedding the reference releases its GPU texture and admission
    // reservation (Qt otherwise keeps a hidden window's scene graph, and with it the renderer and its texture).
    void nativeTextureReleasedOnClose() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        if (!nativeRhi(h)) QSKIP("GPU texture admission needs a native RHI backend; the software backend has none.");
        auto* window = referenceWindow(h); auto* canvas = referenceCanvas(h);
        const qint64 one = h.viewer.image().sizeInBytes();
        QCOMPARE(h.viewer.image().format(), QImage::Format_RGBA8888);   // otherwise the canvas never admits a texture
        AdmissionFloor floor(window, canvas);

        h.viewer.trigger("reference-pin");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(canvas->admittedTextureBytes(), one);
        h.viewer.trigger("reference-close");
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(canvas->admittedTextureBytes(), qint64(0));

        // Re-pinning after the release renders again and holds exactly one texture.
        h.viewer.trigger("reference-pin");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(canvas->admittedTextureBytes(), one);
        // Pin replacement keeps exactly one texture.
        QVERIFY(step(h, 2));
        h.viewer.trigger("reference-pin");
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        QTRY_COMPARE(canvas->image().cacheKey(), h.viewer.image().cacheKey());
        QTest::qWait(100);
        QCOMPARE(canvas->admittedTextureBytes(), one);
        // The window's own close button releases it too.
        window->close();
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(canvas->admittedTextureBytes(), qint64(0));
        QVERIFY2(floor.lowest.load() >= 0, qPrintable(QString::number(floor.lowest.load())));
    }

    void nativeTextureReleasedOnShed() {
        BudgetScope budget;
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        if (!nativeRhi(h)) QSKIP("GPU texture admission needs a native RHI backend; the software backend has none.");
        auto* window = referenceWindow(h); auto* canvas = referenceCanvas(h);
        const qint64 one = h.viewer.image().sizeInBytes();
        QCOMPARE(h.viewer.image().format(), QImage::Format_RGBA8888);
        AdmissionFloor floor(window, canvas);
        glancelot::DisplayManager::setImageBudgetBytes(one + 16);

        h.viewer.trigger("reference-pin");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(canvas->admittedTextureBytes(), one);
        QSignalSpy shed(h.viewer.displayManager(), &glancelot::DisplayManager::referenceShed);
        QVERIFY(step(h, 2));   // a newer current frame sheds the reference
        QCOMPARE(shed.count(), 1);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(canvas->admittedTextureBytes(), qint64(0));
        QVERIFY2(floor.lowest.load() >= 0, qPrintable(QString::number(floor.lowest.load())));
    }
};

QTEST_MAIN(ReferencePinTests)
#include "reference_pin_tests.moc"
