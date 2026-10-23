// STACK-F040-PINNED: the F040 reference window and the MONITOR-OUTPUT pinned output are distinct commands that share
// the display manager's aggregate retained-image budget. Parity cases: close, pin replacement, recovery while both are
// active, and the budget's refusal/shedding paths. Every case checks that the selection never changes.
#include "ui_test_support.hpp"
#include "glancelot/display_manager.hpp"
#include "presentation_window.hpp"
#include <QSet>
#include <QSignalSpy>

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
        QVERIFY(pinBoth(h));
        const QImage first = h.viewer.image();
        QVERIFY(step(h, 2));
        const Selection selection(h.viewer);
        QSignalSpy pinned(dm, &glancelot::DisplayManager::pinnedPathChanged);

        // Replacing the reference does not touch the output pin.
        h.viewer.trigger("reference-pin");
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        QCOMPARE(referenceCanvas(h)->image().cacheKey(), h.viewer.image().cacheKey());
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QCOMPARE(pinned.count(), 0);
        QCOMPARE(dm->retainedImageBytes(), 2 * one);   // the old reference frame is released; p1 stays as the pin
        QVERIFY(selection.same(h.viewer));

        // Replacing the output pin does not touch the reference.
        QSignalSpy reference(&h.viewer, &ViewerController::referenceChanged);
        h.viewer.trigger("presentation-pin");
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(reference.count(), 0);
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        QVERIFY(selection.same(h.viewer));

        // Stale requests from an older selection generation are refused by both pins.
        QVERIFY(!dm->setReferenceImage(media.path(0), first, 0));
        dm->pinImage(media.path(0), first, 0);
        QCOMPARE(h.viewer.referenceName(), QString("p2.png"));
        QCOMPARE(dm->pinnedPath(), media.path(1));
        QCOMPARE(reference.count(), 0);
        QVERIFY(selection.same(h.viewer));
        h.viewer.trigger("presentation-close"); flushDeletes();
    }

    void recoveryWhileBothActive() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media));
        auto* dm = h.viewer.displayManager();
        const qint64 one = h.viewer.image().sizeInBytes();
        QVERIFY(pinBoth(h));
        QVERIFY(step(h, 2));
        const Selection selection(h.viewer);

        // Low-memory release drops optional history only; both explicit pins survive.
        dm->releaseCachedImages();
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QCOMPARE(dm->retainedImageBytes(), 2 * one);

        // Emergency recovery returns everything to the controller: the output closes, the reference window stays.
        h.viewer.trigger("monitor-recover"); flushDeletes();
        QTRY_VERIFY(!dm->isPresentationActive());
        QVERIFY(referenceWindow(h)->isVisible());
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QCOMPARE(referenceCanvas(h)->image().cacheKey(), dm->referenceImage().cacheKey());
        QCOMPARE(dm->pinnedPath(), media.path(0));
        QVERIFY(dm->retainedImageBytes() <= 2 * one);
        QVERIFY(selection.same(h.viewer));

        // Reopening the output after recovery shows its pin again; the reference is unchanged.
        QVERIFY(h.viewer.displayManager()->openPresentation({}, glancelot::PresentationMode::Pinned));
        auto* output = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(output);
        QCOMPARE(output->primaryCanvas()->image().cacheKey(), dm->referenceImage().cacheKey());
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
};

QTEST_MAIN(ReferencePinTests)
#include "reference_pin_tests.moc"
