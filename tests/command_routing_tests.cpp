// EN-06: a command reached through the shipped Main.qml executes exactly once, whichever side (controller or QML)
// owns it, and Main.qml loads without QML warnings.
#include "glancelot/animation_controller.hpp"
#include "ui_test_support.hpp"
#include <QFile>
#include <QMutex>
#include <QSignalSpy>
#include <functional>

namespace {
// QML warnings and errors seen while a case runs. Everything is still passed on to the previous handler (QTest's).
QMutex warningLock;
QStringList qmlWarnings;
QtMessageHandler previousHandler = nullptr;
void captureQmlWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const bool fromQml = (context.file && QByteArray(context.file).endsWith(".qml")) || message.contains(QLatin1String(".qml:"))
        || QByteArray(context.category ? context.category : "").startsWith("qml") || QByteArray(context.category ? context.category : "").startsWith("js");
    if (type != QtDebugMsg && type != QtInfoMsg && fromQml) { QMutexLocker lock(&warningLock); qmlWarnings << message; }
    if (previousHandler) previousHandler(type, context, message);
}
QStringList takeQmlWarnings() { QMutexLocker lock(&warningLock); return std::exchange(qmlWarnings, {}); }

// A disposable 2-frame 2x2 GIF89a (100 ms and 200 ms) beside a still photo, so animation commands have frames to step.
struct AnimatedMedia {
    QTemporaryDir dir;
    AnimatedMedia() {
        static const unsigned char gif[] = {
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
            0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00,
            0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
            0x02, 0x02, 0x44, 0x01, 0x00,
            0x21, 0xF9, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
            0x02, 0x02, 0x4C, 0x01, 0x00, 0x3B};
        QFile file(gifPath());
        if (file.open(QIODevice::WriteOnly)) file.write(reinterpret_cast<const char*>(gif), sizeof(gif));
        QImage still(640, 480, QImage::Format_RGB32);
        still.fill(QColor(90, 120, 160));
        still.save(dir.filePath("still.png"));
    }
    QString gifPath() const { return dir.filePath("animated.gif"); }
};

bool invoke(Harness& h, const QString& id) { return QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, id)); }

// Opens the GIF and leaves its playback paused on frame 0 at 1x with looping on, so every step below is deterministic.
bool openPausedAnimation(Harness& h, const AnimatedMedia& media) {
    h.viewer.setFilmstripVisible(false);
    h.viewer.open(media.gifPath());
    auto* animation = h.viewer.animationController();
    if (!QTest::qWaitFor([&] { return h.viewer.imageReady() && animation->isActive() && animation->frameCount() == 2; }, 15000)) return false;
    animation->pause(); animation->jumpToFrame(0); animation->setSpeed(1.0); animation->setLoop(true);
    return !animation->isPlaying() && animation->currentFrame() == 0;
}

// One routed command: its observable effect must change exactly once. Run twice, each of these lands somewhere
// else (a toggle flips back, a 2-frame step wraps, 1.5x compounds to 2.25x).
struct RoutedCase {
    QString id;
    int key;                              // the default binding, pressed on the canvas (0: no default key)
    std::function<QObject*()> sender;     // the object whose notify signal counts executions
    const char* signal;
    std::function<bool()> applied;        // the state after exactly one execution
    std::function<void()> reset;          // back to the paused baseline before the next case
};
QList<RoutedCase> routedCases(Harness& h) {
    auto* a = h.viewer.animationController();
    auto anim = [a] { return static_cast<QObject*>(a); };
    auto viewer = [&h] { return static_cast<QObject*>(&h.viewer); };
    auto baseline = [a] { a->pause(); a->jumpToFrame(0); a->setSpeed(1.0); a->setLoop(true); };
    return {
        {"toggle-metadata", Qt::Key_I, viewer, SIGNAL(metadataVisibleChanged()),
         [&h] { return h.viewer.isMetadataVisible(); }, [&h] { h.viewer.setMetadataVisible(false); }},
        {"animation-toggle-play-pause", Qt::Key_Space, anim, SIGNAL(playingChanged()), [a] { return a->isPlaying(); }, baseline},
        {"animation-next-frame", Qt::Key_Period, anim, SIGNAL(currentFrameChanged()), [a] { return a->currentFrame() == 1; }, baseline},
        {"animation-previous-frame", Qt::Key_Comma, anim, SIGNAL(currentFrameChanged()), [a] { return a->currentFrame() == 1; }, baseline},
        {"animation-speed-up", Qt::Key_BracketRight, anim, SIGNAL(speedChanged()), [a] { return qFuzzyCompare(a->speed(), 1.5); }, baseline},
        {"animation-speed-down", Qt::Key_BracketLeft, anim, SIGNAL(speedChanged()),
         [a] { return qFuzzyCompare(a->speed(), 1.0 / 1.5); }, baseline},
        {"animation-toggle-loop", 0, anim, SIGNAL(loopChanged()), [a] { return !a->isLoop(); }, baseline},
    };
}
} // namespace

class CommandRoutingTests final : public QObject {
    Q_OBJECT
    QTemporaryDir* config{};
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
        previousHandler = qInstallMessageHandler(captureQmlWarnings);
    }
    void cleanupTestCase() { qInstallMessageHandler(previousHandler); }
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); takeQmlWarnings(); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr; }

    // Explicit routes (toolbar, command search, menus) all go through Main.qml invoke -> viewer.trigger.
    void explicitRouteExecutesOnce() {
        AnimatedMedia media; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openPausedAnimation(h, media));
        for (const auto& c : routedCases(h)) {
            QSignalSpy executions(c.sender(), c.signal);
            QVERIFY2(invoke(h, c.id), qPrintable(c.id));
            QCoreApplication::processEvents();
            QVERIFY2(executions.size() == 1, qPrintable(QString("%1 executed %2 times").arg(c.id).arg(executions.size())));
            QVERIFY2(c.applied(), qPrintable(c.id));
            c.reset();
        }
    }
    // The default keys reach the same commands through the window Shortcuts.
    void defaultKeysExecuteOnce() {
        AnimatedMedia media; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openPausedAnimation(h, media)); QVERIFY(activate(h));
        focusCanvas(h); QTRY_VERIFY(h.canvas()->hasActiveFocus());
        for (const auto& c : routedCases(h)) {
            if (!c.key) continue;
            WAIT_FIRST_KEY(h, h.viewer.bindingFor(c.id));
            QSignalSpy executions(c.sender(), c.signal);
            QTest::keyClick(h.window, Qt::Key(c.key));
            QTRY_VERIFY2(executions.size() >= 1, qPrintable(c.id));
            QTest::qWait(50);
            QVERIFY2(executions.size() == 1, qPrintable(QString("%1 executed %2 times").arg(c.id).arg(executions.size())));
            QVERIFY2(c.applied(), qPrintable(c.id));
            c.reset();
        }
    }
    // Storage profiles are controller state: the command applies once and is not re-broadcast to the view, which
    // has nothing to do for it.
    void storageProfileIsControllerOnly() {
        Harness h; QVERIFY(h.window);
        QSignalSpy changes(&h.viewer, &ViewerController::storageProfileChanged);
        QSignalSpy commands(&h.viewer, &ViewerController::viewCommand);
        for (const QString profile : {"hdd", "network", "ssd", "auto"}) {
            changes.clear();
            QVERIFY(invoke(h, "storage-profile-" + profile));
            QCOMPARE(h.viewer.storageProfile(), profile);
            QCOMPARE(changes.size(), 1);
        }
        for (const auto& emitted : commands) QVERIFY2(!emitted.first().toString().startsWith("storage-profile-"), qPrintable(emitted.first().toString()));
    }
    // Loading the window and opening the shortcut cheat sheet (whose key badges once raised a ReferenceError)
    // logs no QML warning.
    void mainQmlLoadsWithoutQmlWarnings() {
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QTest::qWait(200);
        QVERIFY(invoke(h, "shortcuts-help"));
        auto* sheet = h.object("shortcutCheatSheet"); QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("opened").toBool());
        QTest::qWait(300);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QTRY_VERIFY(!h.window->property("modalOpen").toBool());
        const QStringList warnings = takeQmlWarnings();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

QTEST_MAIN(CommandRoutingTests)
#include "command_routing_tests.moc"
