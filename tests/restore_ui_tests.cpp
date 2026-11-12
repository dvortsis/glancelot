// B9 (F057): the workspace restore banner in the shipped Main.qml. It never takes focus on its own; Ctrl+Shift+R (the
// workspace-restore command) reaches it from the keyboard, Tab moves between its actions, Return acts, Escape dismisses
// and focus returns to the content. Its text is exposed to assistive technology. SAFE mode is shown and restores nothing.
// The engine paths are in restore_offer_tests; here the controller's offer is fed a recovery point directly.
#include "restore_offer.hpp"
#include "ui_test_support.hpp"
#include <QAccessible>
#include <QMutex>
#include <QUuid>
#include <utility>

using glancelot::RestoreOffer;
using glancelot::recovery::ResumeState;
using glancelot::recovery::TabLocation;

namespace {
QMutex warningLock;
QStringList qmlWarnings;
QtMessageHandler previousHandler = nullptr;
void captureQmlWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QByteArray category(context.category ? context.category : "");
    const bool fromQml = (context.file && QByteArray(context.file).endsWith(".qml")) || message.contains(QLatin1String(".qml:"))
        || category.startsWith("qml") || category.startsWith("js");
    if (type != QtDebugMsg && type != QtInfoMsg && fromQml) { QMutexLocker lock(&warningLock); qmlWarnings << message; }
    if (previousHandler) previousHandler(type, context, message);
}
QStringList takeQmlWarnings() { QMutexLocker lock(&warningLock); return std::exchange(qmlWarnings, {}); }

// <temp>/here/p1..p2.png is what the user opened; <temp>/before/x.png and <temp>/later hold the recovered tabs.
struct Folders {
    QTemporaryDir dir;
    Folders() {
        for (const auto* name : {"here/p1.png", "here/p2.png", "before/x.png", "later/y.png"}) {
            QDir(dir.path()).mkpath(QFileInfo(dir.filePath(name)).path());
            QImage image(640, 480, QImage::Format_RGB32); image.fill(QColor::fromHsv(qHash(QByteArray(name)) % 360, 90, 170)); image.save(dir.filePath(name));
        }
    }
    QString path(const char* name) const { return QDir::cleanPath(dir.filePath(name)); }
};
ResumeState crashed(const Folders& f) {
    ResumeState state; state.priorDirty = true; state.hasPosition = true;
    state.checkpointRevision = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.tabs = {{f.path("before"), f.path("before/x.png")}, {f.path("later"), {}}}; state.activeTab = 1;
    state.folder = f.path("later");
    return state;
}
RestoreOffer* offerOf(Harness& h) { return qobject_cast<RestoreOffer*>(h.viewer.workspaceRestore()); }
QQuickItem* banner(Harness& h) { return named(h.window, "workspaceRestore"); }
bool bannerShown(Harness& h) { auto* item = banner(h); return item && item->isVisible() && item->opacity() == 1.0; }
bool focusIn(Harness& h, const char* name) { auto* item = named(h.window, name); return item && item->hasActiveFocus(); }
bool bannerHasFocus(Harness& h) { auto* item = h.window->activeFocusItem(); for (; item; item = item->parentItem()) if (item == banner(h)) return true; return false; }
QString text(Harness& h, const char* name) { auto* item = named(h.window, name); return item ? item->property("text").toString() : QString{}; }
bool visibleItem(Harness& h, const char* name) { auto* item = named(h.window, name); return item && item->isVisible(); }
// The window opened a photo explicitly, with the canvas focused; then the claim's begin reports a crashed workspace.
bool offered(Harness& h, const Folders& f, const QTemporaryDir& config) {
    h.viewer.setFilmstripVisible(false);
    h.viewer.open(f.path("here/p1.png"));
    if (!QTest::qWaitFor([&] { return h.viewer.imageReady(); }, 15000)) return false;
    focusCanvas(h);
    offerOf(h)->setSettingsDirectory(config.path());
    offerOf(h)->recovered(crashed(f));
    return QTest::qWaitFor([&] { return bannerShown(h) && offerOf(h)->state() == "offer"; }, 10000);
}
void pressRestoreShortcut(Harness& h) { QTest::keyClick(h.window, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier); }
} // namespace

class RestoreUiTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
        previousHandler = qInstallMessageHandler(captureQmlWarnings);
    }
    void init() {
        config = std::make_unique<QTemporaryDir>(); QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8());
        takeQmlWarnings();
    }
    void cleanup() {
        qunsetenv("GLANCELOT_CONFIG_DIR"); config.reset();
        const auto warnings = takeQmlWarnings();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void hiddenUntilThereIsSomethingToRestore() {
        Harness h; QVERIFY(h.window); h.window->show(); QVERIFY(activate(h));
        QVERIFY(banner(h));
        QVERIFY(!bannerShown(h));
        // The command says so instead of doing nothing.
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QStringLiteral("workspace-restore"))));
        QTRY_VERIFY_WITH_TIMEOUT(h.window->property("splitMessage").toString().contains("no previous workspace"), 10000);
    }

    // The banner does not take focus: the arrow keys keep browsing what was opened.
    void offerNeverTakesFocusOnItsOwn() {
        Folders f; Harness h; QVERIFY(h.window); h.window->show(); QVERIFY(activate(h));
        QVERIFY(offered(h, f, *config));
        QVERIFY(!bannerHasFocus(h));
        QVERIFY(h.canvas()->hasActiveFocus());
        QCOMPARE(text(h, "workspaceRestoreTitle"), QString("Restore your previous tabs?"));
        QVERIFY(text(h, "workspaceRestoreBody").contains("2 tabs"));
        QVERIFY(text(h, "workspaceRestoreBody").contains("later"));
        QTest::keyClick(h.window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && h.viewer.filePath().endsWith("p2.png"), 10000);
        QVERIFY(bannerShown(h));
    }

    void keyboardReachesTheOfferAndRestores() {
        Folders f; Harness h; QVERIFY(h.window); h.window->show(); QVERIFY(activate(h));
        QVERIFY(offered(h, f, *config));
        WAIT_FIRST_KEY(h, "Ctrl+Shift+R");
        pressRestoreShortcut(h);
        QTRY_VERIFY_WITH_TIMEOUT(focusIn(h, "workspaceRestoreAccept"), 10000);
        QTest::keyClick(h.window, Qt::Key_Tab);
        QTRY_VERIFY_WITH_TIMEOUT(focusIn(h, "workspaceRestoreDismiss"), 10000);
        QTest::keyClick(h.window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(focusIn(h, "workspaceRestoreAccept"), 10000);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.tabs().size(), qsizetype(3), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!bannerShown(h) && h.viewer.imageReady() && h.viewer.filePath().endsWith("y.png"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.canvas()->hasActiveFocus(), 10000); // focus returns to the content
        QVERIFY(h.window->property("splitMessage").toString().contains("Restored 2 tabs"));
        QVERIFY(h.viewer.tabs().at(0).toMap().value("image").toString().endsWith("p1.png")); // what was opened stays
    }

    void escapeDismissesAndIsRemembered() {
        Folders f; Harness h; QVERIFY(h.window); h.window->show(); QVERIFY(activate(h));
        QVERIFY(offered(h, f, *config));
        WAIT_FIRST_KEY(h, "Ctrl+Shift+R");
        pressRestoreShortcut(h);
        QTRY_VERIFY_WITH_TIMEOUT(focusIn(h, "workspaceRestoreAccept"), 10000);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!bannerShown(h), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.canvas()->hasActiveFocus(), 10000);
        QVERIFY(h.viewer.tabs().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!RestoreOffer::readDismissedRevision(RestoreOffer::dismissalPath(config->path(), 0)).isEmpty(), 10000);
        QVERIFY(h.viewer.filePath().endsWith("p1.png"));
    }

    void bannerIsExposedToAssistiveTechnology() {
        Folders f; Harness h; QVERIFY(h.window); h.window->show(); QVERIFY(activate(h));
        QVERIFY(offered(h, f, *config));
        auto* panel = named(h.window, "workspaceRestorePanel"); QVERIFY(panel);
        auto* panelAccess = QAccessible::queryAccessibleInterface(panel); QVERIFY(panelAccess);
        QCOMPARE(panelAccess->role(), QAccessible::AlertMessage);
        QCOMPARE(panelAccess->text(QAccessible::Name), QString("Restore your previous tabs?"));
        QVERIFY(panelAccess->text(QAccessible::Description).contains("closed unexpectedly"));
        auto* accept = QAccessible::queryAccessibleInterface(named(h.window, "workspaceRestoreAccept")); QVERIFY(accept);
        QCOMPARE(accept->role(), QAccessible::Button);
        QCOMPARE(accept->text(QAccessible::Name), QString("Restore tabs"));
        auto* dismiss = QAccessible::queryAccessibleInterface(named(h.window, "workspaceRestoreDismiss")); QVERIFY(dismiss);
        QCOMPARE(dismiss->text(QAccessible::Name), QString("Dismiss"));
        QCOMPARE(dismiss->text(QAccessible::Description), QString("Don't offer this workspace again"));
    }

    void safeModeIsShownWithOnlyDismiss() {
        Harness h; QVERIFY(h.window); h.viewer.setSafeMode(true); h.window->show(); QVERIFY(activate(h));
        QTRY_VERIFY_WITH_TIMEOUT(bannerShown(h), 10000);
        QCOMPARE(text(h, "workspaceRestoreTitle"), QString("Safe mode: nothing was restored"));
        QVERIFY(!visibleItem(h, "workspaceRestoreAccept"));
        QVERIFY(visibleItem(h, "workspaceRestoreDismiss"));
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QStringLiteral("workspace-restore"))));
        QTRY_VERIFY_WITH_TIMEOUT(focusIn(h, "workspaceRestoreDismiss"), 10000);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!bannerShown(h), 10000);
        QVERIFY(h.viewer.tabs().isEmpty());
        QVERIFY(h.viewer.filePath().isEmpty());
    }

    // F052/F053 minimum window: the banner stays inside the window, stacks its actions and keeps them reachable.
    void fitsTheMinimumWindow() {
        Folders f; Harness h; QVERIFY(h.window); h.window->resize(540, 360); h.window->show(); QVERIFY(activate(h));
        QTRY_COMPARE_WITH_TIMEOUT(h.window->width(), 540, 10000);
        QVERIFY(offered(h, f, *config));
        auto* item = banner(h);
        const auto box = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        QVERIFY2(box.left() >= 0 && box.right() <= h.window->width() && box.bottom() <= h.window->height(),
                 qPrintable(QString("banner %1,%2 %3x%4").arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height())));
        for (const char* name : {"workspaceRestoreAccept", "workspaceRestoreDismiss"}) {
            auto* button = named(h.window, name); QVERIFY(button && button->isVisible());
            const auto at = button->mapRectToScene(QRectF(0, 0, button->width(), button->height()));
            QVERIFY2(box.contains(at), name);
        }
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, sceneCenter(named(h.window, "workspaceRestoreDismiss")));
        QTRY_VERIFY_WITH_TIMEOUT(!bannerShown(h), 10000);
    }
};

QTEST_MAIN(RestoreUiTests)
#include "restore_ui_tests.moc"
