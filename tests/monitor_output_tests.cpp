#include <QtTest>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QColorSpace>
#include <QColorTransform>
#include <QMouseEvent>
#include <QWheelEvent>
#include "glancelot/display_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "presentation_window.hpp"
#include "image_canvas.hpp"

class MonitorOutputTests : public QObject {
    Q_OBJECT

    static QImage createTestImage(int width, int height, const QColor& color) {
        QImage img(width, height, QImage::Format_RGBA8888);
        img.fill(color);
        return img;
    }

private slots:
    void testUnvisitedOutputStartsWithDefaults() {
        glancelot::PresentationWindow window; window.resize(800,600); window.setComparisonMode(true);
        window.setPrimaryImage(createTestImage(640,480,Qt::red)); window.setSecondaryImage(createTestImage(640,480,Qt::blue));
        window.viewportCommand("linked"); window.viewportCommand("reference"); window.viewportCommand("zoom-in"); window.viewportCommand("pan-up");
        window.restoreViewport({}); // no saved state on the newly selected identity
        QCOMPARE(window.primaryCanvas()->fitMode(),QStringLiteral("shrink"));
        QCOMPARE(window.secondaryCanvas()->fitMode(),QStringLiteral("shrink"));
        QVERIFY(window.viewportState()["linked"].toBool()); QVERIFY(!window.viewportState()["activeSecondary"].toBool());
    }
    void testTextureBudgetTracksPresentationMode() {
        glancelot::PresentationWindow window;
        QCOMPARE(window.primaryCanvas()->textureBudget(),glancelot::kPresentationTextureBudget);
        QCOMPARE(window.secondaryCanvas()->textureBudget(),qint64(0));
        const QImage image=createTestImage(6400,6144,Qt::red); // generated 150 MiB: exceeds the old 128 MiB half-cap
        QCOMPARE(image.sizeInBytes(),qint64(150)*1024*1024);
        window.setPrimaryImage(image);
        QCOMPARE(window.primaryCanvas()->image().cacheKey(),image.cacheKey());
        QVERIFY(window.primaryCanvas()->image().sizeInBytes()<=window.primaryCanvas()->textureBudget());
        window.setComparisonMode(true);
        QCOMPARE(window.primaryCanvas()->textureBudget(),glancelot::kPresentationTextureBudget/2);
        QCOMPARE(window.secondaryCanvas()->textureBudget(),glancelot::kPresentationTextureBudget/2);
        window.setComparisonMode(false);
        QCOMPARE(window.primaryCanvas()->textureBudget(),glancelot::kPresentationTextureBudget);
        QCOMPARE(window.secondaryCanvas()->textureBudget(),qint64(0));
        QVERIFY(window.secondaryCanvas()->image().isNull());
    }
    void testClosedOutputDoesNotRetainNavigationHistory() {
        glancelot::DisplayManager dm; QVERIFY(dm.openPresentation());
        const auto pinned=createTestImage(40,40,Qt::red);
        dm.pinImage("pin",pinned,1); dm.closePresentation();
        dm.updateImage("a",createTestImage(80,80,Qt::green),2);
        const auto current=createTestImage(64,64,Qt::blue);
        dm.updateImage("b",current,3);
        QCOMPARE(dm.retainedImageBytes(),pinned.sizeInBytes()+current.sizeInBytes());
        QVERIFY(dm.openPresentation()); dm.compareWithPinnedOrPrevious();
        auto* window=qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow()); QVERIFY(window);
        QCOMPARE(window->secondaryCanvas()->image().cacheKey(),pinned.cacheKey());
        dm.closePresentation(); flushDeletes();
    }
    void testIndependentReferenceViewSurvivesTemporarilyMissingImage() {
        glancelot::PresentationWindow window; window.resize(800,600); window.setComparisonMode(true);
        window.setPrimaryImage(createTestImage(640,480,Qt::red));
        window.setSecondaryImage(createTestImage(320,240,Qt::green));
        window.viewportCommand("linked"); window.viewportCommand("reference");
        window.viewportCommand("actual"); window.viewportCommand("zoom-in"); window.viewportCommand("pan-up");
        const auto saved=window.secondaryCanvas()->viewState();
        window.setComparisonMode(false); window.setSecondaryImage({});
        window.setComparisonMode(true); window.setSecondaryImage(createTestImage(320,240,Qt::blue));
        const auto restored=window.secondaryCanvas()->viewState();
        for (const auto* key:{"zoom","centerX","centerY"}) QVERIFY2(qAbs(restored[key].toDouble()-saved[key].toDouble())<1e-6,key);
        const auto state=window.viewportState();
        window.setSecondaryImage({}); window.restoreViewport(state);
        window.setSecondaryImage(createTestImage(320,240,Qt::yellow));
        const auto reopened=window.secondaryCanvas()->viewState();
        for (const auto* key:{"zoom","centerX","centerY"}) QVERIFY2(qAbs(reopened[key].toDouble()-saved[key].toDouble())<1e-6,key);
    }
    void testColorStatusNotifiesWhenPresentationCloses() {
        glancelot::DisplayManager dm; QVERIFY(dm.openPresentation());
        QSignalSpy changed(&dm,&glancelot::DisplayManager::outputColorStatusChanged);
        dm.closePresentation();
        QCOMPARE(dm.outputColorStatus(),QStringLiteral("Presentation closed"));
        QCOMPARE(changed.count(),1); flushDeletes();
    }
    void testNativeDisplayIdentityStateRestore() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_MONITOR_NATIVE_COLOR_PROOF")) QSKIP("Requires native display topology; headless state tests do not qualify mixed DPI");
        glancelot::DisplayManager dm;
        const auto displays=dm.displays();
        if (displays.size()<2) QSKIP("Two native displays unavailable");
        dm.updateImage("generated-native",createTestImage(640,480,Qt::red),1);
        QVERIFY(dm.openPresentation(displays[0].id));
        auto* window=qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow()); QVERIFY(window);
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(),10000);
        dm.outputViewportCommand("actual"); dm.outputViewportCommand("zoom-in"); dm.outputViewportCommand("pan-right");
        const auto first=window->primaryCanvas()->viewState();
        QVERIFY(dm.openPresentation(displays[1].id));
        QTRY_COMPARE(glancelot::DisplayManager::computeDisplayId(window->screen()),displays[1].id);
        QTest::qWait(100);
        // First visit must not inherit A's manual view or comparison interaction state.
        QCOMPARE(window->primaryCanvas()->fitMode(),QStringLiteral("shrink"));
        QVERIFY(window->viewportState()["linked"].toBool()); QVERIFY(!window->viewportState()["activeSecondary"].toBool());
        dm.outputViewportCommand("actual"); dm.outputViewportCommand("zoom-out"); dm.outputViewportCommand("pan-up");
        const auto second=window->primaryCanvas()->viewState();
        QVERIFY(dm.openPresentation(displays[0].id)); QTest::qWait(100);
        auto restored=window->primaryCanvas()->viewState();
        qInfo()<<"native display0 DPR"<<displays[0].devicePixelRatio<<"saved"<<first<<"restored"<<restored;
        for (const auto* key:{"zoom","centerX","centerY"}) QVERIFY2(qAbs(restored[key].toDouble()-first[key].toDouble())<1e-6,key);
        QVERIFY(dm.openPresentation(displays[1].id)); QTest::qWait(100);
        restored=window->primaryCanvas()->viewState();
        qInfo()<<"native display1 DPR"<<displays[1].devicePixelRatio<<"saved"<<second<<"restored"<<restored;
        for (const auto* key:{"zoom","centerX","centerY"}) QVERIFY2(qAbs(restored[key].toDouble()-second[key].toDouble())<1e-6,key);
        qInfo()<<"native output profile status"<<dm.outputColorStatus();
        dm.closePresentation(); flushDeletes();
    }

    void testNativeGpuColorPatch() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_MONITOR_NATIVE_COLOR_PROOF")) QSKIP("Opt-in native GPU readback; headless state tests do not qualify presentation pixels");
        glancelot::PresentationWindow window;
        const QRect available=QGuiApplication::primaryScreen()->availableGeometry();
        window.setGeometry(QRect(available.center()-QPoint(200,150),QSize(400,300)));
        auto source=createTestImage(64,64,QColor(180,120,70)); source.setColorSpace(QColorSpace::SRgb);
        const auto expected=source.convertedToColorSpace(QColorSpace::DisplayP3).pixelColor(32,32);
        window.setPrimaryImage(source); window.setFitMode("fit"); window.setOutputColorProfile(QColorSpace::DisplayP3);
        QTRY_VERIFY_WITH_TIMEOUT(!window.outputColorAtlas().isNull(),15000);
        window.show(); QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),10000);
        QImage pixels;
        auto matches=[&] {
            pixels=window.grabWindow(); if(pixels.isNull()) return false;
            const auto actual=pixels.pixelColor(pixels.width()/2,pixels.height()/2);
            return qAbs(actual.red()-expected.red())<=4 && qAbs(actual.green()-expected.green())<=4 && qAbs(actual.blue()-expected.blue())<=4;
        };
        QTRY_VERIFY_WITH_TIMEOUT(matches(),10000);
        qInfo()<<"native output shader patch"<<pixels.pixelColor(pixels.width()/2,pixels.height()/2)<<"Qt reference"<<expected<<"DPR"<<window.effectiveDevicePixelRatio();
        window.hide();
    }
    void testOutputViewportSurvivesNavigationAndReopen() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        const auto id=dm.activeDisplayId();
        dm.updateImage("generated-a",createTestImage(640,480,Qt::red),1);
        dm.outputViewportCommand("actual");
        auto* window=qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow()); QVERIFY(window);
        QCOMPARE(window->primaryCanvas()->zoom(),qreal(1));
        dm.outputViewportCommand("zoom-in"); dm.outputViewportCommand("pan-right");
        const auto state=window->primaryCanvas()->viewState();
        QCOMPARE(state["zoom"].toDouble(),1.25);
        dm.updateImage("generated-b",createTestImage(640,480,Qt::blue),2);
        QCOMPARE(window->primaryCanvas()->viewState(),state);
        dm.closePresentation(); QVERIFY(dm.openPresentation(id));
        window=qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow()); QVERIFY(window);
        QCOMPARE(window->primaryCanvas()->viewState(),state);
        // Distinct manager/controller output state cannot overwrite this output's state.
        glancelot::DisplayManager other; QVERIFY(other.openPresentation());
        other.setOutputFitMode("fill"); QCOMPARE(window->primaryCanvas()->viewState(),state);
        other.closePresentation(); dm.closePresentation(); flushDeletes();
    }
    void testComparisonViewportsLinkAndSeparate() {
        glancelot::PresentationWindow window; window.resize(800,600);
        window.setComparisonMode(true);
        window.setPrimaryImage(createTestImage(640,480,Qt::red));
        window.setSecondaryImage(createTestImage(320,240,Qt::green));
        window.viewportCommand("actual"); window.viewportCommand("zoom-in"); window.viewportCommand("pan-right");
        QCOMPARE(window.primaryCanvas()->zoom(),window.secondaryCanvas()->zoom());
        const auto a=window.primaryCanvas()->viewState(),b=window.secondaryCanvas()->viewState();
        QVERIFY(qAbs(a["centerX"].toDouble()/640-b["centerX"].toDouble()/320)<1e-6);
        window.viewportCommand("linked");
        const auto independent=window.secondaryCanvas()->viewState();
        window.viewportCommand("zoom-in"); window.viewportCommand("pan-up");
        QCOMPARE(window.secondaryCanvas()->viewState(),independent);
        const auto primary=window.primaryCanvas()->viewState();
        window.viewportCommand("reference"); window.viewportCommand("zoom-in");
        QCOMPARE(window.primaryCanvas()->viewState(),primary);
        QVERIFY(window.secondaryCanvas()->viewState()!=independent);
        window.viewportCommand("linked");
        QCOMPARE(window.primaryCanvas()->zoom(),window.secondaryCanvas()->zoom());
    }
    void testActualSizeUsesPhysicalPixels() {
        glancelot::PresentationWindow window; window.resize(800,600);
        window.setPrimaryImage(createTestImage(320,240,Qt::red)); window.viewportCommand("actual");
        const qreal dpr=window.effectiveDevicePixelRatio();
        const auto a=window.primaryCanvas()->imagePointAt(400,300),b=window.primaryCanvas()->imagePointAt(401,300);
        QCOMPARE(window.primaryCanvas()->zoom(),qreal(1));
        QVERIFY(qAbs((b.x()-a.x())-dpr)<1e-6);
        qInfo()<<"actual-size physical-pixel ratio"<<dpr<<"source pixels per logical pixel"<<b.x()-a.x();
    }
    void testOutputProfileTransitionsAreBoundedAndLatestWins() {
        glancelot::PresentationWindow window;
        auto source=createTestImage(40,40,QColor(180,120,70)); const auto key=source.cacheKey();
        window.setPrimaryImage(source);
        window.setOutputColorProfile(QColorSpace::DisplayP3);
        QTRY_VERIFY_WITH_TIMEOUT(!window.outputColorAtlas().isNull(),15000);
        QCOMPARE(window.outputColorAtlas().sizeInBytes(),glancelot::output_color::atlasBytes);
        QCOMPARE(window.colorResourceBytes(),glancelot::kPresentationColorReservation);
        QCOMPARE(window.primaryCanvas()->image().cacheKey(),key); // no full-raster copy/mutation
        window.setOutputColorProfile(QColorSpace::AdobeRgb);
        window.setOutputColorProfile(QColorSpace::SRgb);
        QTRY_VERIFY_WITH_TIMEOUT(window.outputColorStatus().startsWith("SDR output profile:"),15000);
        QVERIFY(window.outputColorAtlas().isNull()); QCOMPARE(window.colorResourceBytes(),glancelot::kPresentationColorReservation);
        window.setOutputColorProfile({});
        QTRY_VERIFY_WITH_TIMEOUT(window.outputColorStatus().contains("unavailable"),15000);
        QVERIFY(window.outputColorAtlas().isNull());
    }
    void testGeneratedColorAtlasMatchesSdrReferencePatches() {
        const QColorSpace srgb(QColorSpace::SRgb),p3(QColorSpace::DisplayP3);
        const auto atlas=glancelot::output_color::makeAtlas(p3);
        QVERIFY(!atlas.isNull());
        const auto transform=srgb.transformationToColorSpace(p3);
        for (int r : {0,4,16,24,32}) for (int g : {0,4,16,24,32}) for (int b : {0,4,16,24,32}) {
            const QColor input(qRound(r*255.0/32),qRound(g*255.0/32),qRound(b*255.0/32));
            const QColor expected=transform.map(input),actual=atlas.pixelColor(b*33+r,g);
            QVERIFY(qAbs(actual.red()-expected.red())<=1 && qAbs(actual.green()-expected.green())<=1 && qAbs(actual.blue()-expected.blue())<=1);
        }
        QVERIFY(glancelot::output_color::makeAtlas(srgb).isNull());
        QVERIFY(glancelot::output_color::makeAtlas({}).isNull());
    }
    void testDismissChordExpiresAndBindingChangesCancelIt() {
        glancelot::DisplayManager dm; QString binding="Ctrl+K, P";
        dm.setToggleBindingProvider([&] { return binding; }); QVERIFY(dm.openPresentation());
        auto* window=dm.presentationWindow();
        QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier); QTest::qWait(1550);
        QTest::keyClick(window,Qt::Key_P); QVERIFY(dm.isPresentationActive());
        QTest::keyClick(window,Qt::Key_K,Qt::ControlModifier); binding="Ctrl+J, P";
        QTest::keyClick(window,Qt::Key_P); QVERIFY(dm.isPresentationActive());
        QTest::keyClick(window,Qt::Key_J,Qt::ControlModifier);
        QFocusEvent focusOut(QEvent::FocusOut); QCoreApplication::sendEvent(window,&focusOut);
        QTest::keyClick(window,Qt::Key_P); QVERIFY(dm.isPresentationActive());
        binding="F9";
        QKeyEvent repeat(QEvent::KeyPress,Qt::Key_F9,Qt::NoModifier,QString(),true);
        QCoreApplication::sendEvent(window,&repeat); QVERIFY(dm.isPresentationActive());
        QTest::keyClick(window,Qt::Key_F9); QVERIFY(!dm.isPresentationActive()); flushDeletes();
    }
    void testReboundOutputCommandActsOnlyOnPresentation() {
        ViewerController controller;
        QVERIFY(controller.setBinding("presentation-view-actual","Ctrl+Alt+1"));
        auto* dm=controller.displayManager(); QVERIFY(dm->openPresentation());
        dm->updateImage("generated",createTestImage(400,300,Qt::red),1);
        auto* window=qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow()); QVERIFY(window);
        QTest::keyClick(window,Qt::Key_1,Qt::ControlModifier|Qt::AltModifier);
        QCOMPARE(window->primaryCanvas()->fitMode(),QStringLiteral("actual"));
        QCOMPARE(window->primaryCanvas()->zoom(),qreal(1));
        controller.trigger("presentation-view-zoom-in"); QCOMPARE(window->primaryCanvas()->zoom(),qreal(1.25));
        dm->closePresentation(); flushDeletes();
    }
    void initTestCase() {
        QVERIFY(qApp != nullptr);
        glancelot::registerPresentationWindowFactory();
    }

    void testDisplayEnumeration() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.displayCount() >= 1);

        const auto displays = dm.displays();
        QCOMPARE(displays.size(), dm.displayCount());

        bool foundPrimary = false;
        for (const auto& d : displays) {
            QVERIFY(!d.id.isEmpty());
            QVERIFY(!d.name.isEmpty());
            QVERIFY(d.geometry.isValid());
            QVERIFY(d.devicePixelRatio > 0);
            QVERIFY(d.refreshRate > 0);
            QVERIFY(d.colorDepth > 0);
            QVERIFY(!d.colorSpace.isEmpty());
            if (d.isPrimary) foundPrimary = true;
        }
        QVERIFY(foundPrimary);

        const auto varList = dm.displaysVariant();
        QCOMPARE(varList.size(), displays.size());
        const auto firstMap = varList.first().toMap();
        QCOMPARE(firstMap.value("id").toString(), displays.first().id);
        QCOMPARE(firstMap.value("name").toString(), displays.first().name);
    }

    void testStableIdentity() {
        const auto screens = QGuiApplication::screens();
        QVERIFY(!screens.isEmpty());

        const QString id1 = glancelot::DisplayManager::computeDisplayId(screens.first());
        const QString id2 = glancelot::DisplayManager::computeDisplayId(screens.first());
        QCOMPARE(id1, id2);
        QVERIFY(!id1.isEmpty());

        const QString nullId = glancelot::DisplayManager::computeDisplayId(nullptr);
        QCOMPARE(nullId, QStringLiteral("none"));
    }

    void testPreferredDisplay() {
        glancelot::DisplayManager dm;
        const auto preferred = dm.preferredPresentationDisplay();
        QVERIFY(!preferred.id.isEmpty());

        const auto screens = QGuiApplication::screens();
        if (screens.size() > 1) {
            QVERIFY(!preferred.isPrimary);
        } else {
            QVERIFY(preferred.isPrimary);
        }
    }

    void testOpenCloseLifecycle() {
        glancelot::DisplayManager dm;
        QVERIFY(!dm.isPresentationActive());
        QVERIFY(dm.presentationWindow() == nullptr);

        bool opened = dm.openPresentation();
        QVERIFY(opened);
        QVERIFY(dm.isPresentationActive());
        QVERIFY(dm.presentationWindow() != nullptr);
        QCOMPARE(dm.presentationWindow()->title(), QStringLiteral("Glancelot - Presentation"));

        dm.closePresentation();
        QVERIFY(!dm.isPresentationActive());
        QVERIFY(dm.presentationWindow() == nullptr);
    }

    void testTogglePresentation() {
        glancelot::DisplayManager dm;
        QVERIFY(!dm.isPresentationActive());

        dm.togglePresentation();
        QVERIFY(dm.isPresentationActive());

        dm.togglePresentation();
        QVERIFY(!dm.isPresentationActive());
    }

    void testRoutingModes() {
        glancelot::DisplayManager dm;
        dm.openPresentation();
        QCOMPARE(dm.presentationModeString(), QStringLiteral("mirror"));
        QVERIFY(!dm.isPinned());

        dm.setPresentationMode(QStringLiteral("pinned"));
        QCOMPARE(dm.presentationModeString(), QStringLiteral("pinned"));
        QVERIFY(dm.isPinned());

        dm.setPresentationMode(QStringLiteral("comparison"));
        QCOMPARE(dm.presentationModeString(), QStringLiteral("comparison"));
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw != nullptr);
        QVERIFY(pw->isComparisonMode());

        dm.setPresentationMode(QStringLiteral("mirror"));
        QCOMPARE(dm.presentationModeString(), QStringLiteral("mirror"));
        QVERIFY(!pw->isComparisonMode());

        dm.closePresentation();
    }

    void testMirrorRoutingAndStaleRejection() {
        glancelot::DisplayManager dm;
        dm.openPresentation();

        const QImage img1 = createTestImage(400, 300, Qt::blue);
        dm.updateImage(QStringLiteral("photo1.jpg"), img1, 100);
        QCOMPARE(dm.currentPath(), QStringLiteral("photo1.jpg"));
        QCOMPARE(dm.currentGeneration(), quint64(100));

        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw != nullptr);
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(400, 300));

        // Stale update (generation 50 < 100) must be discarded
        const QImage staleImg = createTestImage(200, 200, Qt::red);
        dm.updateImage(QStringLiteral("stale.jpg"), staleImg, 50);
        QCOMPARE(dm.currentPath(), QStringLiteral("photo1.jpg"));
        QCOMPARE(dm.currentGeneration(), quint64(100));
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(400, 300));

        // Fresh update (generation 101 > 100) must be accepted
        const QImage img2 = createTestImage(800, 600, Qt::green);
        dm.updateImage(QStringLiteral("photo2.jpg"), img2, 101);
        QCOMPARE(dm.currentPath(), QStringLiteral("photo2.jpg"));
        QCOMPARE(dm.currentGeneration(), quint64(101));
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(800, 600));

        dm.closePresentation();
    }

    void testPinnedImageIsolation() {
        glancelot::DisplayManager dm;
        dm.openPresentation();

        const QImage img1 = createTestImage(400, 300, Qt::cyan);
        dm.updateImage(QStringLiteral("ref.jpg"), img1, 10);
        dm.pinCurrentImage();

        QVERIFY(dm.isPinned());
        QCOMPARE(dm.pinnedPath(), QStringLiteral("ref.jpg"));

        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw != nullptr);
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(400, 300));

        // Navigate to a new image in controller; pinned presentation remains unchanged
        const QImage img2 = createTestImage(1200, 800, Qt::yellow);
        dm.updateImage(QStringLiteral("photo2.jpg"), img2, 20);
        QCOMPARE(dm.currentPath(), QStringLiteral("photo2.jpg"));
        // Presentation primary canvas stays on pinned ref.jpg (400x300)
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(400, 300));

        // Unpin restores mirror mode and updates to photo2.jpg
        dm.unpin();
        QVERIFY(!dm.isPinned());
        QCOMPARE(dm.presentationModeString(), QStringLiteral("mirror"));
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(1200, 800));

        dm.closePresentation();
    }

    void testComparisonMode() {
        glancelot::DisplayManager dm;
        dm.openPresentation();

        const QImage imgPrimary = createTestImage(600, 400, Qt::magenta);
        const QImage imgSecondary = createTestImage(600, 400, Qt::gray);

        dm.updateImage(QStringLiteral("primary.jpg"), imgPrimary, 1);
        dm.setComparisonImage(QStringLiteral("secondary.jpg"), imgSecondary, 2);

        QCOMPARE(dm.presentationModeString(), QStringLiteral("comparison"));
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw != nullptr);
        QVERIFY(pw->isComparisonMode());
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(600, 400));
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(600, 400));

        dm.closePresentation();
    }

    void testEscapeDismissal() {
        glancelot::DisplayManager dm;
        dm.openPresentation();
        QVERIFY(dm.isPresentationActive());

        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw != nullptr);

        // Simulate Escape key press
        QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &escapeEvent);

        QVERIFY(!dm.isPresentationActive());
        QVERIFY(dm.presentationWindow() == nullptr);
    }

    void testControllerIntegration() {
        ViewerController controller;
        QVERIFY(controller.displayManager() != nullptr);
        QVERIFY(!controller.isPresentationActive());
        QCOMPARE(controller.presentationMode(), QStringLiteral("mirror"));

        // Verify commands registered
        const auto commands = controller.commands();
        auto hasCommand = [&](const QString& id) {
            for (const auto& cmd : commands) {
                if (cmd.toMap().value("id").toString() == id) return true;
            }
            return false;
        };

        QVERIFY(hasCommand("presentation-toggle"));
        QVERIFY(hasCommand("presentation-mirror"));
        QVERIFY(hasCommand("presentation-pin"));
        QVERIFY(hasCommand("presentation-compare"));
        QVERIFY(hasCommand("presentation-close"));

        QCOMPARE(controller.bindingFor("presentation-toggle"), QStringLiteral("F8"));

        // Trigger toggle command
        controller.trigger("presentation-toggle");
        QVERIFY(controller.isPresentationActive());

        // Trigger pin command
        controller.trigger("presentation-pin");
        QCOMPARE(controller.presentationMode(), QStringLiteral("pinned"));

        // Trigger close command
        controller.trigger("presentation-close");
        QVERIFY(!controller.isPresentationActive());
    }

    // ---- MONITOR-OUTPUT-01 fix pass ----
    static void flushDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    void testIdentityIdenticalMonitorsWithoutSerialAreDistinct() {
        glancelot::DisplayIdentitySource a;
        a.manufacturer = "DEL"; a.model = "U2720Q"; a.platformCode = "DEL40F4";
        a.physicalSizeMm = QSize(600, 340); a.pixelSize = QSize(3840, 2160);
        const auto ids = glancelot::DisplayManager::assignDisplayIds({a, a});
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids[0] != ids[1]);
        // No names at all: pure geometry fallback still separates identical panels.
        glancelot::DisplayIdentitySource g;
        g.physicalSizeMm = QSize(500, 300); g.pixelSize = QSize(1920, 1080);
        const auto gids = glancelot::DisplayManager::assignDisplayIds({g, g, g});
        QCOMPARE(QSet<QString>(gids.begin(), gids.end()).size(), 3);
    }

    void testIdentitySerialSurvivesReorderAndReconnect() {
        glancelot::DisplayIdentitySource a, b;
        a.manufacturer = "DEL"; a.model = "U2720Q"; a.serial = "AAA111"; a.physicalSizeMm = QSize(600, 340);
        b = a; b.serial = "BBB222";
        const auto ab = glancelot::DisplayManager::assignDisplayIds({a, b});
        const auto ba = glancelot::DisplayManager::assignDisplayIds({b, a});
        QCOMPARE(ab[0], ba[1]);
        QCOMPARE(ab[1], ba[0]);
        const auto onlyB = glancelot::DisplayManager::assignDisplayIds({b});   // a unplugged
        QCOMPARE(onlyB[0], ab[1]);
        const auto again = glancelot::DisplayManager::assignDisplayIds({a, b}); // reconnected
        QCOMPARE(again, ab);
    }

    void testColorSpaceIsNotHardcoded() {
        QCOMPARE(glancelot::DisplayManager::colorSpaceName(nullptr), QStringLiteral("Unknown"));
        glancelot::DisplayManager dm;
        const auto screens = QGuiApplication::screens();
        const auto list = dm.displays();
        for (int i = 0; i < list.size(); ++i)
            QCOMPARE(list[i].colorSpace, glancelot::DisplayManager::colorSpaceName(screens[i]));
        QCOMPARE(glancelot::DisplayInfo().colorSpace, QStringLiteral("Unknown"));
    }

    void testCompareCommandSuppliesSecondImage() {
        ViewerController controller;
        auto* dm = controller.displayManager();
        controller.trigger("presentation-toggle");
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow());
        QVERIFY(pw);
        dm->updateImage("a.jpg", createTestImage(300, 200, Qt::red), 1);
        dm->updateImage("b.jpg", createTestImage(640, 480, Qt::blue), 2);
        controller.trigger("presentation-compare");
        QCOMPARE(controller.presentationMode(), QStringLiteral("comparison"));
        QVERIFY(pw->isComparisonMode());
        QCOMPARE(pw->primaryCanvas()->image().size(), QSize(640, 480));
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(300, 200));   // previous image
        // Pinned image takes precedence over previous.
        dm->pinImage("pin.jpg", createTestImage(111, 99, Qt::green), 3);
        controller.trigger("presentation-compare");
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(111, 99));
        controller.trigger("presentation-close");
    }

    void testStalePinAndComparisonRejected() {
        glancelot::DisplayManager dm;
        dm.openPresentation();
        dm.pinImage("p10.jpg", createTestImage(100, 100, Qt::red), 10);
        dm.pinImage("p5.jpg", createTestImage(50, 50, Qt::blue), 5);
        QCOMPARE(dm.pinnedPath(), QStringLiteral("p10.jpg"));
        dm.setComparisonImage("c20.jpg", createTestImage(70, 70, Qt::red), 20);
        dm.setComparisonImage("c7.jpg", createTestImage(30, 30, Qt::blue), 7);
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(70, 70));
        dm.closePresentation();
    }

    void testOutputFitModeCommandAndPerOutputState() {
        ViewerController controller;
        auto* dm = controller.displayManager();
        controller.trigger("presentation-toggle");
        const QString id = dm->activeDisplayId();
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("shrink"));
        controller.trigger("presentation-fit");
        QCOMPARE(pw->fitMode(), QStringLiteral("fit"));
        QCOMPARE(dm->outputFitMode(id), QStringLiteral("fit"));
        QCOMPARE(pw->primaryCanvas()->fitMode(), QStringLiteral("fit"));
        controller.setPresentationFitMode("fill");
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        controller.trigger("presentation-close");
        // Reopen: state is remembered for this output id.
        controller.trigger("presentation-toggle");
        pw = qobject_cast<glancelot::PresentationWindow*>(dm->presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        controller.setPresentationFitMode("bogus");
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        controller.trigger("presentation-close");
    }

    void testAggregateImageBudgetAndCloseRelease() {
        const qint64 saved = glancelot::DisplayManager::imageBudgetBytes();
        const qint64 one = qint64(200) * 200 * 4;
        glancelot::DisplayManager::setImageBudgetBytes(one * 2 + 16);
        glancelot::DisplayManager dm;
        dm.openPresentation();
        dm.updateImage("1", createTestImage(200, 200, Qt::red), 1);
        dm.updateImage("2", createTestImage(200, 200, Qt::green), 2);   // previous = 1
        dm.updateImage("3", createTestImage(200, 200, Qt::blue), 3);    // previous = 2
        QVERIFY(dm.retainedImageBytes() <= one * 2 + 16);
        dm.pinImage("pin", createTestImage(200, 200, Qt::gray), 4);
        dm.setComparisonImage("cmp", createTestImage(200, 200, Qt::cyan), 5);
        QVERIFY(dm.retainedImageBytes() <= one * 2 + 16);
        QCOMPARE(dm.currentPath(), QStringLiteral("3"));          // current is never shed
        glancelot::DisplayManager::setImageBudgetBytes(saved);

        // Shared QImage is counted once.
        const QImage shared = createTestImage(200, 200, Qt::white);
        dm.updateImage("s", shared, 6);
        dm.pinImage("s-pin", shared, 7);
        QVERIFY(dm.retainedImageBytes() <= one * 2);

        dm.setComparisonImage("cmp2", createTestImage(100, 100, Qt::cyan), 8);
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw->primaryCanvas()->textureBudget() + pw->secondaryCanvas()->textureBudget()
                <= glancelot::kPresentationTextureBudget);
        dm.closePresentation();
        QVERIFY(dm.presentationWindow() == nullptr);
        dm.releaseCachedImages();
        QVERIFY(dm.retainedImageBytes() <= one * 2);   // only current + pinned (shared) remain
    }

    void testScreenRemovedAndAddedPaths() {
        glancelot::DisplayManager dm;
        QSignalSpy displaysSpy(&dm, &glancelot::DisplayManager::displaysChanged);
        QSignalSpy closedSpy(&dm, &glancelot::DisplayManager::presentationClosed);
        QScreen* screen = QGuiApplication::primaryScreen();
        dm.openPresentation();
        QVERIFY(dm.isPresentationActive());
        // A different screen disappearing must not close the output.
        emit qGuiApp->screenRemoved(nullptr);
        QVERIFY(dm.isPresentationActive());
        // The output's own screen disappearing closes it exactly once.
        emit qGuiApp->screenRemoved(screen);
        QVERIFY(!dm.isPresentationActive());
        QCOMPARE(closedSpy.count(), 1);
        QVERIFY(dm.presentationWindow() == nullptr);
        QVERIFY(displaysSpy.count() >= 2);
        const int before = displaysSpy.count();
        emit qGuiApp->screenAdded(screen);
        QCOMPARE(displaysSpy.count(), before + 1);
        // Controller side remains usable and can reopen.
        QVERIFY(dm.openPresentation());
        dm.closePresentation();
        flushDeletes();
    }

    void testFailedDecodeAndRepeatedOpenCloseDoNotLeak() {
        flushDeletes();
        const int windowsBefore = QGuiApplication::allWindows().size();
        glancelot::DisplayManager dm;
        for (int i = 0; i < 50; ++i) {
            QVERIFY(dm.openPresentation());
            dm.updateImage(QString("f%1").arg(i), createTestImage(64, 64, Qt::red), quint64(2 * i + 1));
            dm.updateImage(QString(), QImage(), quint64(2 * i + 2));   // failed decode
            QVERIFY(dm.currentPath().isEmpty());
            auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
            QVERIFY(pw);
            QVERIFY(pw->primaryCanvas()->image().isNull());
            dm.closePresentation();
            flushDeletes();
            QCOMPARE(QGuiApplication::allWindows().size(), windowsBefore);
        }
        QVERIFY(!dm.isPresentationActive());
    }

    void testEscapeClosesOnceAndToggleKeyIsRebindable() {
        glancelot::DisplayManager dm;
        QSignalSpy closedSpy(&dm, &glancelot::DisplayManager::presentationClosed);
        dm.openPresentation();
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &esc);
        flushDeletes();
        QCOMPARE(closedSpy.count(), 1);

        dm.setToggleBindingProvider([] { return QStringLiteral("F9"); });
        dm.openPresentation();
        pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QKeyEvent f8(QEvent::KeyPress, Qt::Key_F8, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &f8);          // no longer bound
        QVERIFY(dm.isPresentationActive());
        QKeyEvent f9(QEvent::KeyPress, Qt::Key_F9, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &f9);
        QVERIFY(!dm.isPresentationActive());
        QCOMPARE(closedSpy.count(), 2);
        flushDeletes();
    }

    void testHiddenCanvasMustReleaseShedImage() {
        const qint64 saved = glancelot::DisplayManager::imageBudgetBytes();
        struct Reset { qint64 n; ~Reset() { glancelot::DisplayManager::setImageBudgetBytes(n); } } reset{saved};
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.updateImage("a", createTestImage(40, 40, Qt::red), 1);
        dm.setComparisonImage("b", createTestImage(40, 40, Qt::blue), 2);
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw);
        dm.setMode(glancelot::PresentationMode::Mirror);
        const qint64 one = 40 * 40 * 4;
        glancelot::DisplayManager::setImageBudgetBytes(one);
        dm.updateImage("c", createTestImage(40, 40, Qt::green), 3);
        QCOMPARE(dm.retainedImageBytes(), one);
        dm.releaseCachedImages();
        QVERIFY2(pw->secondaryCanvas()->image().isNull(), "Shed/low-memory-released secondary pixels are retained by the hidden canvas outside accounting");
        dm.closePresentation();
        flushDeletes();
    }

    void testOldComparisonCannotOverrideNewCompareCommand() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.updateImage("a", createTestImage(64, 64, Qt::red), 1);
        dm.updateImage("b", createTestImage(80, 80, Qt::green), 2);
        dm.setComparisonImage("new", createTestImage(40, 40, Qt::blue), 20);
        dm.compareWithPinnedOrPrevious();
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(64, 64));
        dm.setComparisonImage("old", createTestImage(30, 30, Qt::yellow), 7);
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(64, 64));
        dm.closePresentation();
        flushDeletes();
    }

    void testInvalidFitMustNotDestroyRememberedFit() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.setOutputFitMode("fill");
        dm.setOutputFitMode("bogus");
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        dm.closePresentation();
        QVERIFY(dm.openPresentation());
        pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        dm.closePresentation();
        flushDeletes();
    }

    void testChordToggleKeyDismissesAndEscapeCancelsChord() {
        glancelot::DisplayManager dm;
        dm.setToggleBindingProvider([] { return QStringLiteral("Ctrl+K, P"); });
        QSignalSpy closedSpy(&dm, &glancelot::DisplayManager::presentationClosed);
        dm.openPresentation();
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw);

        // Step 1: chord prefix Ctrl+K
        QKeyEvent step1(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        QCoreApplication::sendEvent(pw, &step1);
        QVERIFY(dm.isPresentationActive());

        // Escape cancels chord without closing window
        QKeyEvent escCancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &escCancel);
        QVERIFY(dm.isPresentationActive());
        QCOMPARE(closedSpy.count(), 0);

        // Retrying: step 1 Ctrl+K, then step 2 P -> dismisses!
        QCoreApplication::sendEvent(pw, &step1);
        QVERIFY(dm.isPresentationActive());
        QKeyEvent step2(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier);
        QCoreApplication::sendEvent(pw, &step2);
        QVERIFY(!dm.isPresentationActive());
        QCOMPARE(closedSpy.count(), 1);
        flushDeletes();
    }
};

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("GLANCELOT_MONITOR_NATIVE_COLOR_PROOF")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    MonitorOutputTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "monitor_output_tests.moc"

