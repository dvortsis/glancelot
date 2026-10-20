#include <QtTest>
#include <QGuiApplication>
#include <QScreen>
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
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    MonitorOutputTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "monitor_output_tests.moc"

