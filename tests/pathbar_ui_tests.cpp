// F002: breadcrumbs and path completion in the path bar.
#include "ui_test_support.hpp"
#include <QDir>

static void typeText(QQuickWindow* w, const char* text) { for (const char* c = text; *c; ++c) QTest::keyClick(w, *c); }

class PathBarUiTests final : public QObject {
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

    void breadcrumbsJumpToAncestors() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        const QString here = QDir::cleanPath(h.viewer.directory());
        const int depth = here.split('/', Qt::SkipEmptyParts).size() + (here.startsWith('/') ? 1 : 0);
        auto* bar = named(h.window, "pathBreadcrumbs"); QVERIFY(bar); QTRY_VERIFY(bar->isVisible());
        int crumbs = 0; while (named(h.window, QString("crumb-%1").arg(crumbs))) ++crumbs;
        QCOMPARE(crumbs, depth);
        QQuickItem* last = named(h.window, QString("crumb-%1").arg(crumbs - 1)); QVERIFY(last);
        QVERIFY(last->property("text").toString() == QFileInfo(here).fileName());
        QQuickItem* parent = named(h.window, QString("crumb-%1").arg(crumbs - 2)); QVERIFY(parent);
        QTest::mouseMove(h.window, sceneCenter(parent)); QTest::qWait(50);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(parent), 60);
        const QString expected = QDir::cleanPath(QFileInfo(here).absolutePath());
        QTRY_COMPARE_WITH_TIMEOUT(QDir::cleanPath(h.viewer.directory()), expected, 10000);
    }

    void completionListsTypedScopeAndTabAccepts() {
        QTemporaryDir root; QVERIFY(root.isValid());
        for (const char* name : {"alpha", "alpine", "beta"}) QVERIFY(QDir(root.path()).mkdir(name));
        QFile file(QDir(root.path()).filePath("alone.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QTRY_VERIFY(tree->isVisible());
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree")); // the tree grabs focus when shown; wait it out
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        auto* address = named(h.window, "folderAddress"); QTRY_VERIFY(address->hasActiveFocus());
        address->setProperty("text", root.path() + "/"); address->setProperty("cursorPosition", address->property("text").toString().size());
        typeText(h.window, "al");
        auto names = [&] { return tree->property("suggestions").toStringList(); };
        QTRY_COMPARE(names(), (QStringList{"alpha", "alpine"})); // folders only, prefix match
        QTRY_VERIFY(named(h.window, "pathSuggestions")->isVisible());
        // Newest request wins: the longer prefix replaces the list.
        typeText(h.window, "p");
        QTRY_COMPARE(names(), (QStringList{"alpha", "alpine"}));
        typeText(h.window, "i");
        QTRY_COMPARE(names(), (QStringList{"alpine"}));
        QTest::keyClick(h.window, Qt::Key_Tab);
        QTRY_VERIFY(address->property("text").toString().endsWith("/alpine/"));
        // Escape leaves the field and hides the list; nothing was created or navigated.
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(tree->property("suggestions").toList().isEmpty());
        QVERIFY(!QFileInfo(QDir(root.path()).filePath("alpine/alpine")).exists());
    }
};

QTEST_MAIN(PathBarUiTests)
#include "pathbar_ui_tests.moc"
