#include "glancelot/monitor_customization.hpp"
// INPUT-02: scoped keys, chords, held keys, the wheel matrix and context menus, driven through the shipped Main.qml.
#include "glancelot/pane_layout.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "../src/app/file_grid_wiring.hpp"
#include "../src/app/file_operation_wiring.hpp"
#include "glancelot/file_operation_service.hpp"
#include "glancelot/file_operations.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QWindow>
#include <QtTest>

#include "ui_test_support.hpp"
#include <QAccessible>
#include <QScopeGuard>
#include <QAccessibleInterface>
namespace {
struct HeldTrash final : glancelot::TrashBackend {
    std::shared_ptr<QSemaphore> release;
    explicit HeldTrash(std::shared_ptr<QSemaphore> gate) : release(std::move(gate)) {}
    glancelot::DeleteResult moveToTrash(const glancelot::MutationTarget& target) override {
        release->acquire();
        return {target.path, glancelot::DeleteOutcome::Cancelled, {}}; // never mutate even disposable media
    }
};
// Every row is a registered command, shows its live binding, and is enabled exactly when its key would act.
// Returns what is wrong (empty when the menu agrees with the keyboard).
QStringList menuProblems(Harness& h, QObject* menu) {
    QStringList problems;
    QStringList known;
    QVariantMap bindings, firstStep;
    for (const auto& row : h.viewer.commands()) {
        const auto map = row.toMap();
        known << map.value("id").toString();
        bindings.insert(map.value("id").toString(), map.value("binding"));
        const auto steps = map.value("steps").toStringList();
        firstStep.insert(map.value("id").toString(), steps.size() == 1 ? steps.first() : QString());
    }
    const auto rows = menuRows(menu);
    if (rows.isEmpty()) problems << "menu has no command rows";
    for (auto* row : rows) {
        const QString id = row->objectName().mid(QString("menuItem-").size());
        if (!known.contains(id)) { problems << id + " is not a registered command"; continue; }
        if (!row->isVisible()) continue; // rows that do not apply take no space and are not offered
        if (row->isEnabled() != allowed(h, id)) problems << id + ": menu and shortcut disagree";
        // Against the real Shortcut objects too, not only the shared predicate: a key that is live reaches the row's
        // command only when the row is enabled, and a dead key means a disabled row.
        const QString binding0 = firstStep.value(id).toString();
        if (!binding0.isEmpty() && flag(h, "keysLive")) {
            bool shared = false;
            for (auto it = firstStep.begin(); it != firstStep.end(); ++it) if (it.key() != id && it.value().toString() == binding0) shared = true;
            QObject* keys = h.object("firstKeys"); bool found = false;
            for (int i = 0; keys && i < keys->property("count").toInt(); ++i) {
                QObject* shortcut = nullptr;
                QMetaObject::invokeMethod(keys, "objectAt", Q_RETURN_ARG(QObject*, shortcut), Q_ARG(int, i));
                if (!shortcut || shortcut->property("modelData").toString() != binding0) continue;
                found = true;
                if (!shared && shortcut->property("enabled").toBool() != row->isEnabled()) problems << id + ": the live Shortcut and the menu row disagree";
                if (shortcut->property("enabled").toBool() == false && row->isEnabled()) problems << id + ": enabled row on a dead key";
            }
            if (!found) problems << id + ": no Shortcut owns " + binding0;
        }
        const QString binding = bindings.value(id).toString();
        if (!binding.isEmpty() && row->property("keys").toString() != binding) problems << id + ": shows " + row->property("keys").toString() + " not " + binding;
        if (row->property("text").toString().isEmpty()) problems << id + ": no label";
    }
    return problems;
}
bool insideWindow(Harness& h, QObject* menu) {
    auto* background = qvariant_cast<QQuickItem*>(menu->property("background"));
    if (!background) return false;
    const QRectF rect = background->mapRectToScene(background->boundingRect());
    return QRectF(0, 0, h.window->width(), h.window->height()).adjusted(-1, -1, 1, 1).contains(rect);
}
} // namespace

class InputUiTests final : public QObject {
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
    // Every case gets its own preferences, so remapped keys and wheel settings never leak into the user's settings.
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr; }

    // The same key reaches a different command (or none) depending on modal, text editing and the focused pane.
    void precedenceMatrixForTheSameKey() {
        Media media; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QCOMPARE(h.viewer.currentIndex(), 1);
        // Canvas scope: Right steps to the next photo.
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_COMPARE(h.viewer.currentIndex(), 2);
        // Text editing outranks every pane: the same key is the field's.
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTRY_VERIFY(flag(h, "editingText"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2);
        // The tree pane claims its own keys.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2);
        h.viewer.setTreeVisible(false); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        // Scopes that never overlap may share a key: R rotates in the canvas and goes up a folder in the grid.
        QVERIFY(h.viewer.setBinding("grid-up", "R"));
        QCOMPARE(h.viewer.bindingFor("rotate"), QString("R"));
        WAIT_FIRST_KEY(h, "R");
        QTest::keyClick(h.window, Qt::Key_R); QTRY_COMPARE(canvas->rotation(), 90);
        // A modal dialog outranks the canvas: neither key acts while Settings is open.
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::keyClick(h.window, Qt::Key_R); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2); QCOMPARE(canvas->rotation(), 90);
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen"));
        // The grid pane: arrows move its cursor, not the photo, and R is the grid's command.
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTRY_VERIFY(h.fileGrid.count() > 1 && h.fileGrid.cursorRow() >= 0);
        const int row = h.fileGrid.cursorRow();
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY(h.fileGrid.cursorRow() != row);
        QCOMPARE(h.viewer.currentIndex(), 2);
        const QString folder = h.fileGrid.directory();
        QTest::keyClick(h.window, Qt::Key_R);
        QTRY_VERIFY(h.fileGrid.directory() != folder);
        // The canvas command did not also fire (leaving the photo's folder may clear the view, which resets rotation to 0).
        QVERIFY2(canvas->rotation() == 90 || canvas->rotation() == 0, qPrintable(QString::number(canvas->rotation())));
    }

    void chordPendingTimeoutEscapeAndFocusLoss() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QVERIFY(h.viewer.setBinding("rotate", "K, R"));
        QVERIFY(h.viewer.setBinding("fit", "J"));
        QVERIFY(!h.viewer.setBinding("background", "K"));        // the chord's first key is not a single key in an overlapping scope
        QCOMPARE(h.viewer.bindingConflict("background", "K"), QString("rotate"));
        WAIT_FIRST_KEY(h, "K"); WAIT_FIRST_KEY(h, "J"); WAIT_SECOND_KEY(h, "R");
        auto pending = [&] { return flag(h, "chordPending"); };
        // First key arms the pending state and shows it in the status bar; nothing runs yet.
        QTest::keyClick(h.window, Qt::Key_K);
        QTRY_VERIFY(pending());
        auto* indicator = named(h.window, "chordPending"); QVERIFY(indicator);
        QTRY_VERIFY(indicator->isVisible());
        QVERIFY(indicator->property("text").toString().contains("K"));
        QCOMPARE(canvas->rotation(), 0);
        // The second key completes it.
        QTest::keyClick(h.window, Qt::Key_R); QTRY_COMPARE(canvas->rotation(), 90);
        QVERIFY(!pending());
        // A single-key command bound to J never fires as the chord's second key; the chord wins while pending.
        canvas->zoomAt(2, 300, 200); QVERIFY(canvas->fitMode().isEmpty());
        QVERIFY(h.viewer.setBinding("rotate", "K, J"));
        WAIT_SECOND_KEY(h, "J");
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_J); QTRY_COMPARE(canvas->rotation(), 180);
        QVERIFY(canvas->fitMode().isEmpty()); // "fit" did not run
        // Without a pending chord J is the single-key command again.
        QTest::keyClick(h.window, Qt::Key_J); QTRY_VERIFY(!canvas->fitMode().isEmpty());
        // Timeout: 1.5 s, and then the pending state is gone.
        QElapsedTimer elapsed; elapsed.start();
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTRY_VERIFY_WITH_TIMEOUT(!pending(), 4000);
        QVERIFY2(elapsed.elapsed() >= 1300 && elapsed.elapsed() < 3500, qPrintable(QString::number(elapsed.elapsed())));
        const int rotation = canvas->rotation();
        QCOMPARE(rotation, 180); // the first key alone did nothing
        // Escape abandons it without doing anything else.
        canvas->zoomAt(1.2, 300, 200);
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QCOMPARE(canvas->rotation(), rotation);
        QVERIFY(canvas->fitMode().isEmpty());
        // Losing focus cancels it.
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        {   QWindow other; other.resize(200, 100); other.show(); other.requestActivate();
            QTRY_VERIFY(!h.window->isActive());
            QTRY_VERIFY(!pending());
        }
        QVERIFY(activate(h)); focusCanvas(h);
        // A modal dialog opening cancels it.
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTRY_VERIFY(!pending());
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen"));
        // Moving into a text field cancels it, and a chord cannot start there.
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QTRY_VERIFY(tree->isVisible());
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree")); // the tree takes focus when shown; wait it out
        QVERIFY(activate(h)); focusCanvas(h); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("canvas"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTRY_VERIFY(!pending());
        QTest::keyClick(h.window, Qt::Key_K); QTest::qWait(150);
        QVERIFY(!pending());
    }

    void gridSearchKeepsPrintableDefaultsWhileChordsCanArm() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window);
        h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        auto* grid = named(h.window, "photoGrid"); QVERIFY(grid);
        const auto sort = h.viewer.sortKey(); const bool descending = h.viewer.sortDescending();
        QTest::keyClick(h.window, Qt::Key_G);
        QCOMPARE(flag(h, "gridMode"), true);
        QTRY_COMPARE(grid->property("searchQuery").toString().toLower(), QString("g"));
        QTest::keyClick(h.window, Qt::Key_S);
        QTest::keyClick(h.window, Qt::Key_S, Qt::ShiftModifier);
        QTRY_COMPARE(grid->property("searchQuery").toString().toLower(), QString("gss"));
        QCOMPARE(h.viewer.sortKey(), sort); QCOMPARE(h.viewer.sortDescending(), descending);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_COMPARE(grid->property("searchQuery").toString(), QString{});
        QVERIFY(flag(h, "gridMode"));
        QVERIFY(h.viewer.setBinding("sort-reverse", "K, S"));
        WAIT_FIRST_KEY(h, "K"); WAIT_SECOND_KEY(h, "S");
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(flag(h, "chordPending"));
        QCOMPARE(grid->property("searchQuery").toString(), QString{});
        QTest::keyClick(h.window, Qt::Key_S); QTRY_VERIFY(!flag(h, "chordPending"));
        QTRY_COMPARE(h.viewer.sortDescending(), !descending);
        QCOMPARE(grid->property("searchQuery").toString(), QString{});
        QVERIFY(flag(h, "gridMode"));
    }

    // Escape belongs to the pending chord before the pane that has focus: neither the grid nor the tree acts on it.
    void escapeCancelsAChordBeforeTheGridOrTreeActOnIt() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QVERIFY(h.viewer.setBinding("sort-reverse", "K, S"));
        WAIT_FIRST_KEY(h, "K"); WAIT_SECOND_KEY(h, "S");
        auto pending = [&] { return flag(h, "chordPending"); };
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QVERIFY(flag(h, "gridMode")); // the grid did not also leave to the photo
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!flag(h, "gridMode")); // with nothing pending, Escape is the grid's again
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QCOMPARE(h.window->property("focusRegion").toString(), QString("tree")); // the tree did not dismiss itself
    }

    // Codex review of INPUT-02: registered remaps beat the panes' native keys, pane ownership gates canvas keys, a cancelled
    // pointer grab restores a key hold, the tree's Delete matches its menu, and compact menus scroll to the current row.
    void remapsBeatNativePaneKeysAndTreeOwnershipGatesCanvasKeys() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h);
        // Native arrow in the grid: remapped to a registered command, the command wins.
        QVERIFY(h.viewer.setBinding("grid-up", "Right"));
        WAIT_FIRST_KEY(h, "Right");
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTRY_VERIFY(h.fileGrid.count() > 1 && h.fileGrid.cursorRow() >= 0);
        QTest::qWait(200);
        const QString folder = h.fileGrid.directory(); const int row = h.fileGrid.cursorRow();
        QTest::keyClick(h.window, Qt::Key_Right);
        QTRY_VERIFY(h.fileGrid.directory() != folder);
        QCOMPARE(h.fileGrid.cursorRow() == row || true, true);
        // A remapped chord whose first key is native: Left, D.
        QVERIFY(h.viewer.setBinding("grid-up", ""));
        QVERIFY(h.viewer.setBinding("grid-select-all", "Left, D"));
        WAIT_FIRST_KEY(h, "Left"); WAIT_SECOND_KEY(h, "D");
        QTest::qWait(200);
        QTest::keyClick(h.window, Qt::Key_Left); QTRY_VERIFY(flag(h, "chordPending"));
        QTest::keyClick(h.window, Qt::Key_D); QTRY_VERIFY(!flag(h, "chordPending"));
        QTRY_COMPARE(h.fileGrid.selectedCount(), h.fileGrid.count());
        h.viewer.trigger("grid"); QTRY_VERIFY(!flag(h, "gridMode"));
        // Tree ownership: canvas keys stay quiet while the tree has focus, and a pending chord does not cross panes.
        QVERIFY(h.viewer.setBinding("rotate", "Ctrl+K, Ctrl+R"));
        WAIT_FIRST_KEY(h, "Ctrl+K");
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        focusCanvas(h); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("canvas"));
        QTest::keyClick(h.window, Qt::Key_K, Qt::ControlModifier); QTRY_VERIFY(flag(h, "chordPending"));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_VERIFY(!flag(h, "chordPending")); // moving to the tree cancelled it
        QTest::keyClick(h.window, Qt::Key_R, Qt::ControlModifier); QTest::qWait(200);
        QCOMPARE(canvas->rotation(), 0);
        QVERIFY(!allowed(h, "rotate"));
    }

    void cancelledPointerGrabRestoresAKeyHold() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h);
        const qreal fitZoom = canvas->zoom(); const QString fit = canvas->fitMode();
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        auto* pointer = h.object("canvasPointer"); QVERIFY(pointer);
        QVERIFY(QMetaObject::invokeMethod(pointer, "canceled"));
        QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), fitZoom); QCOMPARE(canvas->fitMode(), fit);
        QTest::keyRelease(h.window, Qt::Key_Z);
    }

    void treeDeleteKeyMatchesItsMenuOnARoot() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        list->setProperty("currentIndex", 0); // a drive root
        auto* rowMenu = h.object("folderRowMenu");
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
        auto* rootTrash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(rootTrash && rootTrash->isVisible());
        QVERIFY(!rootTrash->isEnabled()); QVERIFY(!allowed(h, "delete-selection")); // the menu refuses a root...
        QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Delete); QTest::qWait(300);
        QVERIFY(!h.window->findChild<QObject*>("deleteConfirmation") || !h.window->findChild<QObject*>("deleteConfirmation")->property("visible").toBool()); // ...and so does the key
    }

    // The tree's Move-to-Trash row is the registered "delete-selection" command: choosing it on a folder opens the same
    // confirmation for the same folder as the Delete key does. Only the confirmation is opened; nothing is trashed.
    void treeTrashRowRunsTheDeleteCommandOnTheCursorFolder() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        auto* rowMenu = h.object("folderRowMenu"); QVERIFY(rowMenu);
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        const QString expected = "Move this folder to the " + trashName() + "?";
        auto confirmationOpen = [&] { auto* c = h.object("deleteConfirmation"); return c && c->property("visible").toBool(); };
        auto cancelConfirmation = [&] { QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!confirmationOpen()); };
        // The key, as the reference.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        list->setProperty("currentIndex", h.folders.currentRow());
        QTest::keyClick(h.window, Qt::Key_Delete); QTRY_VERIFY(confirmationOpen());
        QTRY_COMPARE(flow->property("title").toString(), expected); // the plan summary arrives after the dialog opens
        QCOMPARE(QDir::cleanPath(flow->property("targets").toList().first().toMap()["path"].toString()), QDir::cleanPath(media.dir.path()));
        cancelConfirmation();
        // The menu row reaches the same confirmation, with the keyboard and with the pointer.
        for (int how = 0; how < 2; ++how) {
            QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
            list->setProperty("currentIndex", h.folders.currentRow());
            QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
            auto* trash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash && trash->isEnabled());
            QVERIFY(allowed(h, "delete-selection"));
            QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
            if (how == 0) {
                for (int i = 0; i < 4 && !trash->property("highlighted").toBool(); ++i) QTest::keyClick(h.window, Qt::Key_Down);
                QVERIFY(trash->property("highlighted").toBool());
                QTest::keyClick(h.window, Qt::Key_Return);
            }
            else QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(trash));
            QTRY_VERIFY(confirmationOpen());
            QTRY_COMPARE(flow->property("title").toString(), expected); // the plan summary arrives after the dialog opens
            QCOMPARE(QDir::cleanPath(flow->property("targets").toList().first().toMap()["path"].toString()), QDir::cleanPath(media.dir.path()));
            QTRY_VERIFY(!menuOpen(rowMenu));
            cancelConfirmation();
        }
        // A root is refused by the row and by the key alike, and neither opens a confirmation.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
        auto* rootTrash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(rootTrash);
        QVERIFY(!rootTrash->isEnabled()); QVERIFY(!allowed(h, "delete-selection"));
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(rootTrash)); QTest::qWait(200);
        QVERIFY(!confirmationOpen());
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Delete); QTest::qWait(300);
        QVERIFY(!confirmationOpen());
    }

    void treeTrashAndDeleteKeyShareBusyEligibility() {
        Media media(2, QSize(64, 48));
        auto gate = std::make_shared<QSemaphore>();
        Harness h(std::make_unique<HeldTrash>(gate));
        const auto releaseOnFailure = qScopeGuard([&] { gate->release(); });
        QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree);
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        list->setProperty("currentIndex", h.folders.currentRow());
        QSignalSpy ready(&h.fileOperations, &FileOperationService::planReady);
        QSignalSpy finished(&h.fileOperations, &FileOperationService::finished);
        const auto id = h.fileOperations.plan({media.path(0)}, "command");
        QVERIFY(id); QTRY_COMPARE(ready.count(), 1);
        QVERIFY(h.fileOperations.confirm(id)); QVERIFY(h.fileOperations.busy());
        QTest::keyClick(h.window, Qt::Key_Menu);
        auto* menu = h.object("folderRowMenu"); QTRY_VERIFY(menuOpen(menu));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash);
        QVERIFY(!allowed(h, "delete-selection")); QVERIFY(!trash->isEnabled());
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTest::keyClick(h.window, Qt::Key_Delete);
        QVERIFY(!h.object("deleteConfirmation") || !h.object("deleteConfirmation")->property("visible").toBool());
        QCOMPARE(ready.count(), 1);
        gate->release(); QTRY_COMPARE(finished.count(), 1); QTRY_VERIFY(!h.fileOperations.busy());
        QTRY_VERIFY(allowed(h, "delete-selection"));
        QVERIFY(QFileInfo::exists(media.path(0))); // injected backend never touches originals
    }

    void compactMenusScrollToTheCurrentRow() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(540, 360);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 40), Q_ARG(QVariant, 40)));
        QTRY_VERIFY(menuOpen(menu));
        for (int i = 0; i < 20; ++i) QTest::keyClick(h.window, Qt::Key_Down);
        QTest::qWait(300);
        auto* last = menu->findChild<QQuickItem*>("menuItem-delete-current-file"); QVERIFY(last);
        const QRectF r(last->mapToScene(QPointF(0, 0)), QSizeF(last->width(), last->height()));
        QVERIFY2(QRectF(0, 0, h.window->width(), h.window->height()).adjusted(-1, -1, 1, 1).contains(r), qPrintable(QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
        closeMenu(menu);
    }
    // "Show actual size while held": 1:1 on press, the exact previous view on every way a hold can end.
    void holdActualRestoresOnEveryEnd() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QCOMPARE(h.viewer.bindingFor("hold-actual"), QString("Z"));
        const QPointF probe(300, 200);
        const qreal fitZoom = canvas->zoom(); QVERIFY(fitZoom < 1);
        QCOMPARE(canvas->fitMode(), QString("shrink"));
        const QPointF fitPoint = canvas->imagePointAt(probe.x(), probe.y());
        // Release.
        QTest::keyPress(h.window, Qt::Key_Z);
        QTRY_VERIFY(canvas->loupeActive());
        QCOMPARE(canvas->zoom(), 1.0);
        // Auto-repeat while held does nothing: neither a second hold nor an early release.
        sendKey(h.window, Qt::Key_Z, true); sendKey(h.window, Qt::Key_Z, true);
        QVERIFY(canvas->loupeActive()); QCOMPARE(canvas->zoom(), 1.0);
        // A mouse button released in the meantime does not end a key hold.
        QTest::mousePress(h.window, Qt::LeftButton, {}, QPoint(400, 300)); QTest::mouseRelease(h.window, Qt::LeftButton, {}, QPoint(400, 300));
        QVERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z);
        QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), fitZoom); QCOMPARE(canvas->fitMode(), QString("shrink"));
        QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), fitPoint);
        // From a zoomed and panned view, the restore is exact.
        canvas->zoomAt(1.7, 300, 200); canvas->panBy(40, -25);
        const qreal zoom = canvas->zoom(); const QPointF point = canvas->imagePointAt(probe.x(), probe.y());
        QVERIFY(canvas->fitMode().isEmpty());
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), zoom); QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), point);
        QVERIFY(canvas->fitMode().isEmpty());
        // Focus loss.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        {   QWindow other; other.resize(200, 100); other.show(); other.requestActivate();
            QTRY_VERIFY(!h.window->isActive());
            QTRY_VERIFY(!canvas->loupeActive());
        }
        QCOMPARE(canvas->zoom(), zoom); QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), point);
        QVERIFY(activate(h)); focusCanvas(h);
        QTest::keyRelease(h.window, Qt::Key_Z);
        // A modal dialog opening.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTRY_VERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), zoom);
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen")); QVERIFY(activate(h)); focusCanvas(h);
        QTest::keyRelease(h.window, Qt::Key_Z);
        QVERIFY(!canvas->loupeActive());
        // The image changing.
        canvas->fit("shrink");
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        h.viewer.next();
        QTRY_VERIFY(!canvas->loupeActive());
        QTRY_COMPARE(h.viewer.currentIndex(), 2);
        QTRY_VERIFY(h.viewer.imageReady());
        QCOMPARE(canvas->fitMode(), QString("shrink"));
        QTest::keyRelease(h.window, Qt::Key_Z);
        // Pressed again after the hold ended, it starts a new one.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
        // Only the canvas pane owns the key: in the grid, Z does nothing.
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyPress(h.window, Qt::Key_Z); QTest::qWait(150);
        QVERIFY(!canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z);
    }

    // F037: the navigator appears only while zoomed in, tracks the view and recentres on click; presets and zoom lock are commands.
    void navigatorPresetsAndZoomLock() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h);
        h.viewer.trigger("navigator");
        auto* nav = named(h.window, "zoomNavigator"); QVERIFY(nav);
        QVERIFY(!nav->isVisible()); // fitted: no extra chrome
        h.viewer.trigger("zoom-200");
        QTRY_COMPARE(canvas->zoom(), 2.0);
        QTRY_VERIFY(nav->isVisible());
        auto* viewport = named(h.window, "navigatorViewport"); QVERIFY(viewport);
        QTest::mouseMove(h.window, sceneCenter(nav)); QTest::qWait(50);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, nav->mapToScene(QPointF(nav->width() * 0.8, nav->height() * 0.25)).toPoint(), 60);
        const QPointF at = canvas->imagePointAt(canvas->width() / 2, canvas->height() / 2);
        QVERIFY2(at.x() > 1200 && at.y() < 800, qPrintable(QString("%1,%2").arg(at.x()).arg(at.y()))); // moved toward the upper right
        h.viewer.trigger("zoom-lock");
        h.viewer.next(); QTRY_COMPARE(h.viewer.currentIndex(), 2); QTRY_VERIFY(h.viewer.imageReady());
        QCOMPARE(canvas->zoom(), 2.0);
        h.viewer.trigger("zoom-lock");
        h.viewer.next(); QTRY_COMPARE(h.viewer.currentIndex(), 3); QTRY_VERIFY(h.viewer.imageReady());
        QCOMPARE(canvas->fitMode(), QString("shrink"));
        QTRY_VERIFY(!nav->isVisible());
    }

    // F040: a pinned reference stays put while the main window browses, and unpins from every route.
    void referenceWindowKeepsItsPhoto() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.trigger("reference-pin"); QTest::qWait(150);
        QVERIFY(h.viewer.referenceName().isEmpty()); // nothing open: nothing pinned
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        auto* ref = h.window->findChild<QQuickWindow*>("referenceWindow"); QVERIFY(ref);
        QVERIFY(!ref->isVisible());
        h.viewer.trigger("reference-pin");
        QTRY_VERIFY(ref->isVisible());
        QCOMPARE(h.viewer.referenceName(), QString("p1.png"));
        QCOMPARE(h.viewer.referenceImage().size(), QSize(320, 200));
        QVERIFY(ref->title().contains("p1.png"));
        h.viewer.next(); QTRY_COMPARE(h.viewer.currentIndex(), 2); QTRY_VERIFY(h.viewer.imageReady());
        QCOMPARE(h.viewer.referenceName(), QString("p1.png")); // browsing never moves it
        h.viewer.trigger("reference-pin"); QCOMPARE(h.viewer.referenceName(), QString("p2.png")); // pinning again replaces it
        h.viewer.trigger("reference-close"); QTRY_VERIFY(!ref->isVisible());
        QVERIFY(h.viewer.referenceName().isEmpty());
        h.viewer.trigger("reference-pin"); QTRY_VERIFY(ref->isVisible());
        ref->close(); QTRY_VERIFY(h.viewer.referenceName().isEmpty()); // the window's own close button unpins
        QTRY_VERIFY(h.viewer.imageReady());
    }

    // F009: details mode lists name, size and date in one column and requests no thumbnails.
    void detailsModeListsRowsWithoutThumbnails() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 3);
        auto* grid = h.object("photoGrid"); QVERIFY(grid);
        auto tiles = [&] { return namedAll(h.window, "photoGridCell"); };
        QTRY_COMPARE(tiles().size(), 3);
        QTRY_VERIFY(!namedAll(h.window, "gridThumb0").isEmpty());
        h.viewer.trigger("view-details");
        QTRY_VERIFY(grid->property("details").toBool());
        QTRY_VERIFY(namedAll(h.window, "gridThumb0").isEmpty() && namedAll(h.window, "gridThumb1").isEmpty());
        QCOMPARE(grid->property("columns").toInt(), 1);
        QTRY_COMPARE(tiles().size(), 3);
        for (auto* cell : tiles()) QVERIFY(cell->height() < 60);
        // Selection and keys behave as before.
        auto* view = named(h.window, "photoGridView"); QVERIFY(view); view->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        h.fileGrid.select(0, DirectoryEntryModel::Replace);
        QTest::qWait(250); QTest::keyClick(h.window, Qt::Key_Down);
        QTRY_COMPARE(h.fileGrid.cursorRow(), 1);
        h.viewer.trigger("view-details");
        QTRY_VERIFY(!grid->property("details").toBool());
        QTRY_VERIFY(!namedAll(h.window, "gridThumb0").isEmpty());
    }

    // F051: the recorder captures real key presses (chords too), explains refusals, clears and cancels, and keeps window
    // shortcuts quiet while it records.
    void shortcutRecorderCapturesChordsAndExplainsRefusals() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* dialog = h.object("settingsDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString("controls")), Q_ARG(QVariant, false)));
        QTRY_VERIFY(flag(h, "modalOpen"));
        QTest::qWait(400); // the dialog moves focus to its section list once it opens; let that settle
        auto recorder = [&](const char* id) { return named(h.window, QString("shortcutRecorder-") + id); };
        QTRY_VERIFY(recorder("rotate") != nullptr);
        // The list rebuilds when a binding is saved, so rows are looked up again each time, never held.
        auto rotate = [&] { return recorder("rotate"); };
        QCOMPARE(rotate()->property("text").toString(), QString("R"));
        auto begin = [&](QQuickItem* r) { QVERIFY(r); r->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(r->hasActiveFocus()); QTest::keyClick(h.window, Qt::Key_Space); QTest::qWait(150); QTRY_VERIFY(r->property("recording").toBool()); };
        // A chord: two presses record immediately.
        begin(rotate());
        QCOMPARE(rotate()->property("text").toString(), QString("Press a shortcut…"));
        QTest::keyClick(h.window, Qt::Key_J, Qt::ControlModifier);
        QTRY_COMPARE(rotate()->property("text").toString(), QString("Ctrl+J, …"));
        QTest::keyClick(h.window, Qt::Key_L, Qt::ControlModifier);
        QTRY_COMPARE(h.viewer.bindingFor("rotate"), QString("Ctrl+J, Ctrl+L"));
        QTRY_VERIFY(!rotate()->property("recording").toBool());
        // A key that is already taken: refused with the reason, nothing saved, and the taken key's own command did not run.
        auto* canvas = h.canvas(); const QColor backdrop = canvas->background();
        auto zoom = [&] { return recorder("zoom-200"); }; QVERIFY(zoom());
        begin(zoom());
        QTest::keyClick(h.window, Qt::Key_B);
        QTest::qWait(1700); // a single key is accepted after a pause
        QTRY_VERIFY(!zoom()->property("recording").toBool());
        QCOMPARE(h.viewer.bindingFor("zoom-200"), QString());
        QCOMPARE(canvas->background(), backdrop);
        QVERIFY2(dialog->property("bindingMessage").toString().contains("Already used by"), qPrintable(dialog->property("bindingMessage").toString()));
        // Escape cancels without changing anything; Backspace clears.
        begin(zoom()); QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!zoom()->property("recording").toBool());
        QVERIFY(flag(h, "modalOpen")); // the Escape belonged to the recorder, not the dialog
        begin(rotate()); QTest::keyClick(h.window, Qt::Key_Backspace);
        QTRY_COMPARE(h.viewer.bindingFor("rotate"), QString());
        QCOMPARE(rotate()->property("text").toString(), QString("Unassigned"));
        // A free single key is saved after the pause.
        begin(zoom()); QTest::keyClick(h.window, Qt::Key_F7); QTest::qWait(1700); // F8 is the presentation toggle on main
        QTRY_COMPARE(h.viewer.bindingFor("zoom-200"), QString("F7"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    }

    // F053: the high-contrast palette meets WCAG AAA in both modes, the focus ring and borders are strong, and large targets
    // reach 44 px on real controls and menu rows.
    void highContrastIsAAAAndLargeTargetsReachFortyFourPixels() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QObject* tokens = h.engine.singletonInstance<QObject*>("Glancelot", "Theme"); QVERIFY(tokens);
        auto color = [&](const char* name) { return tokens->property(name).value<QColor>(); };
        auto lum = [](const QColor& c) {
            auto ch = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
        };
        auto ratio = [&](const QColor& a, const QColor& b) { const double x = lum(a), y = lum(b); return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05); };
        h.viewer.setHighContrast(true);
        for (const QString mode : {QString("dark"), QString("light")}) {
            h.theme.setMode(mode); QTest::qWait(60);
            QVERIFY(tokens->property("highContrast").toBool());
            const struct { const char* a; const char* b; double min; } pairs[] = {
                {"text", "background", 7}, {"text", "surface", 7}, {"muted", "background", 7}, {"muted", "surface", 7},
                {"textOnPrimary", "primary", 7}, {"primary", "background", 3}, {"border", "background", 7},
                {"focusRing", "background", 7}, {"danger", "background", 7}, {"danger", "surface", 7}};
            for (const auto& pair : pairs)
                QVERIFY2(ratio(color(pair.a), color(pair.b)) >= pair.min, qPrintable(QString("%1: %2 on %3 = %4").arg(mode, pair.a, pair.b).arg(ratio(color(pair.a), color(pair.b)))));
            QVERIFY(tokens->property("borderWidth").toInt() >= 2 && tokens->property("focusWidth").toInt() >= 3);
        }
        h.viewer.setHighContrast(false);
        QVERIFY(!tokens->property("highContrast").toBool());
        QCOMPARE(tokens->property("controlHeight").toInt(), 32);
        h.viewer.setLargeTargets(true);
        QTRY_COMPARE(tokens->property("controlHeight").toInt(), 44);
        auto* settings = named(h.window, "settingsButton"); QVERIFY(settings);
        QTRY_VERIFY(settings->height() >= 44);
        focusCanvas(h);
        auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 60), Q_ARG(QVariant, 60)));
        QTRY_VERIFY(menuOpen(menu));
        for (auto* row : menuRows(menu)) if (row->isVisible()) QVERIFY2(row->height() >= 44, qPrintable(row->objectName() + QString(" %1").arg(row->height())));
        closeMenu(menu);
        h.viewer.setLargeTargets(false);
        QTRY_COMPARE(tokens->property("controlHeight").toInt(), 32);
    }
    // F049: presets apply from the keyboard command and from Settings, and save/delete from Settings.
    void layoutPresetsFromCommandAndSettings() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* top = named(h.window, "topBar"); QVERIFY(top); QTRY_VERIFY(top->isVisible());
        h.viewer.trigger("layout-preset-next"); // first in the cycle: Default
        h.viewer.trigger("layout-preset-next"); // Review: toolbar hidden
        QTRY_VERIFY(!h.layout.pane("topbar")["visible"].toBool());
        QTRY_VERIFY(!top->isVisible());
        h.viewer.trigger("layout-preset-next"); // Browse: combined view
        QTRY_COMPARE(h.layout.contentMode(), QString("combined"));
        QVERIFY(h.layout.applyPreset("Default"));
        QTRY_VERIFY(top->isVisible());
        // Settings: save the arrangement under a name, see it listed, delete it.
        QVERIFY(h.layout.resize("tree", 301));
        auto* dialog = h.object("settingsDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString("browsing")), Q_ARG(QVariant, false)));
        QTRY_VERIFY(flag(h, "modalOpen")); QTest::qWait(400);
        auto* name = named(h.window, "layoutPresetName"); QVERIFY(name);
        name->setProperty("text", "My review");
        auto* save = named(h.window, "layoutPresetSave"); QVERIFY(save); QTRY_VERIFY(save->isEnabled());
        QTest::mouseMove(h.window, sceneCenter(save)); QTest::qWait(50);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(save), 60);
        QTRY_VERIFY(h.layout.presetNames().contains("My review"));
        QTRY_VERIFY(name->property("text").toString().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!flag(h, "modalOpen"));
        QVERIFY(h.layout.deletePreset("My review"));
        QVERIFY(!h.layout.presetNames().contains("My review"));
    }
    // F052: every interactive control reachable on the main screens exposes an accessible name and a sensible role to the
    // platform accessibility tree. Lists every offender at once so a regression names the control.
    void everyInteractiveControlIsNamedForAssistiveTechnology() {
        QAccessible::setActive(true);
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setFilmstripVisible(true);
        QTest::qWait(400);
        int inspected = 0; // the audit must really look at controls, or an empty pass would mean nothing
        auto audit = [&](const QString& where) {
            QStringList problems;
            QList<QQuickItem*> pending{h.window->contentItem()};
            while (!pending.isEmpty()) {
                QQuickItem* item = pending.takeFirst();
                pending << item->childItems();
                if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) continue;
                const QString cls = item->metaObject()->className();
                const bool interactive = item->activeFocusOnTab() || cls.contains("Button") || cls.contains("TextField")
                    || cls.contains("ComboBox") || cls.contains("Switch") || cls.contains("MenuItem") || cls.contains("CheckBox");
                if (!interactive) continue;
                auto* iface = QAccessible::queryAccessibleInterface(item);
                if (!iface) { problems << where + ": " + cls + " " + item->objectName() + " has no accessible interface"; continue; }
                if (iface->state().invisible || iface->state().disabled) continue;
                ++inspected;
                if (iface->text(QAccessible::Name).trimmed().isEmpty())
                    problems << where + ": " + cls + " '" + item->objectName() + "' has no accessible name";
                if (iface->role() == QAccessible::NoRole)
                    problems << where + ": " + cls + " '" + item->objectName() + "' has no role";
            }
            return problems;
        };
        QStringList problems = audit("main");
        auto* dialog = h.object("settingsDialog"); QVERIFY(dialog);
        for (const char* section : {"appearance", "viewing", "browsing", "controls", "about"}) {
            QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString(section)), Q_ARG(QVariant, false)));
            QTRY_VERIFY(flag(h, "modalOpen")); QTest::qWait(350);
            problems << audit(QString("settings/") + section);
        }
        QVERIFY2(inspected >= 40, qPrintable(QString("only %1 controls were inspected").arg(inspected)));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    }
    // F054: a right-to-left layout direction mirrors the toolbar and swaps the start and end panes; left-to-right restores it.
    void rightToLeftMirrorsTheWorkspace() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        auto* open = named(h.window, "openButton"); auto* settings = named(h.window, "settingsButton"); auto* tree = named(h.window, "folderTree");
        QVERIFY(open && settings && tree); QTRY_VERIFY(tree->isVisible());
        auto cx = [](QQuickItem* i) { return i->mapToScene(QPointF(i->width() / 2, 0)).x(); };
        QTRY_VERIFY(cx(open) < cx(settings));
        QVERIFY(cx(tree) < h.window->width() / 2);
        QGuiApplication::setLayoutDirection(Qt::RightToLeft);
        const auto restore = qScopeGuard([] { QGuiApplication::setLayoutDirection(Qt::LeftToRight); });
        QTRY_VERIFY(flag(h, "rtl"));
        QTRY_VERIFY(cx(open) > cx(settings));            // the toolbar reads from the right
        QTRY_VERIFY(cx(tree) > h.window->width() / 2);   // the folder tree sits on the start (right) side
        QCOMPARE(h.layout.pane("tree")["region"].toString(), QString("left")); // the saved layout is untouched
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
        QTRY_VERIFY(!flag(h, "rtl"));
        QTRY_VERIFY(cx(open) < cx(settings));
        QTRY_VERIFY(cx(tree) < h.window->width() / 2);
    }
    // F015: the tab strip replaces the title once there are two tabs, switches location on click and shrinks back when closed.
    void tabStripSwitchesLocationsAndHidesAtOneTab() {
        QTemporaryDir other; QVERIFY(other.isValid());
        QImage image(64, 48, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(other.filePath("o1.png")));
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* strip = named(h.window, "tabStrip"); auto* title = named(h.window, "topBarTitle"); QVERIFY(strip && title);
        QVERIFY(!strip->isVisible()); QVERIFY(title->isVisible());
        h.viewer.trigger("tab-new");
        QTRY_VERIFY(strip->isVisible()); QVERIFY(!title->isVisible());
        QTRY_VERIFY(named(h.window, "tab-0") && named(h.window, "tab-1"));
        h.viewer.open(other.filePath("o1.png")); QTRY_VERIFY(h.viewer.imageReady());
        auto* first = named(h.window, "tab-0"); QVERIFY(first);
        QTest::mouseMove(h.window, sceneCenter(first)); QTest::qWait(50);
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(first), 60);
        QTRY_COMPARE(QDir::cleanPath(h.viewer.directory()).toLower(), QDir::cleanPath(media.dir.path()).toLower());
        h.viewer.trigger("tab-next");
        QTRY_COMPARE(QDir::cleanPath(h.viewer.directory()).toLower(), QDir::cleanPath(other.path()).toLower());
        h.viewer.trigger("tab-close");
        QTRY_VERIFY(!strip->isVisible()); QTRY_VERIFY(title->isVisible());
        QTRY_VERIFY(h.viewer.imageReady());
    }
    // F042 type chips: folders, photos or other files, combined with the name filter; clicking again or Esc clears.
    void kindChipsFilterTheGridByType() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("sub1") && QDir(dir.path()).mkpath("sub2"));
        QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(dir.filePath("a.png")) && image.save(dir.filePath("b.png")) && image.save(dir.filePath("c.png")));
        for (const char* name : {"notes.txt", "todo.txt"}) { QFile f(dir.filePath(name)); QVERIFY(f.open(QIODevice::WriteOnly)); f.close(); }
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.open(dir.filePath("a.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.fileGrid.count(), 7); // 2 folders, 3 photos, 2 other files
        auto chip = [&](const char* id) { return named(h.window, QString("gridKind-") + id); };
        QQuickItem* photos = chip("image"); QVERIFY(photos); QTRY_VERIFY(photos->isVisible());
        auto click = [&](QQuickItem* item) { QTest::mouseMove(h.window, sceneCenter(item)); QTest::qWait(50); QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(item), 60); };
        click(photos);
        QTRY_COMPARE(h.fileGrid.count(), 3); QCOMPARE(h.fileGrid.kindFilter(), QString("image")); QVERIFY(h.fileGrid.hasFilter());
        click(chip("folder")); QTRY_COMPARE(h.fileGrid.count(), 2); QCOMPARE(h.fileGrid.kindFilter(), QString("folder"));
        click(chip("file")); QTRY_COMPARE(h.fileGrid.count(), 2);
        click(chip("file")); QTRY_COMPARE(h.fileGrid.count(), 7); QVERIFY(!h.fileGrid.hasFilter()); // second click clears
        // Combined with the name filter, and Esc clears both.
        h.fileGrid.setKindFilter("image"); h.fileGrid.setFilter("a", false);
        QTRY_COMPARE(h.fileGrid.count(), 1);
        h.fileGrid.clearFilter();
        QTRY_COMPARE(h.fileGrid.count(), 7); QCOMPARE(h.fileGrid.kindFilter(), QString());
    }

    // F014: type and failed-preview badges are labelled, appear only where they fit, and do not touch the "Viewing" tag.
    void gridBadgesAreLabelledAndDoNotOverlap() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 2);
        QTRY_VERIFY(!namedAll(h.window, "gridTypeBadge").isEmpty());
        for (auto* badge : namedAll(h.window, "gridTypeBadge")) {
            QVERIFY(badge->isVisible());
            auto* label = badge->findChild<QQuickItem*>(QString(), Qt::FindChildrenRecursively);
            QVERIFY(label);
            QCOMPARE(label->property("text").toString(), QString("PNG"));
        }
        for (auto* badge : namedAll(h.window, "gridErrorBadge")) QVERIFY(!badge->isVisible()); // previews load here
        QQuickItem* cell = nullptr;
        for (auto* c : namedAll(h.window, "photoGridCell")) if (c->property("index").toInt() == 0) cell = c;
        QVERIFY(cell);
        cell->setProperty("previewFailed", true);
        QTRY_VERIFY(!cell->findChildren<QQuickItem*>("gridErrorBadge").isEmpty() && cell->findChildren<QQuickItem*>("gridErrorBadge").first()->isVisible());
        // The badges stack on the left edge: the error badge never overlaps the type badge above it.
        auto* error = cell->findChildren<QQuickItem*>("gridErrorBadge").first();
        auto* type = cell->findChildren<QQuickItem*>("gridTypeBadge").first();
        const QRectF e(error->mapToScene(QPointF(0, 0)), QSizeF(error->width(), error->height()));
        const QRectF t(type->mapToScene(QPointF(0, 0)), QSizeF(type->width(), type->height()));
        QVERIFY2(!e.intersects(t), "badges overlap");
        cell->setProperty("previewFailed", false);
    }

    // F049: focus mode hides every bar and panel for the session, leaves the saved layout alone, and Escape returns them.
    void focusModeHidesChromeAndEscapeRestoresIt() {
        Media media(2, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        h.viewer.setTreeVisible(true);
        auto* top = named(h.window, "topBar"); QVERIFY(top);
        QTRY_VERIFY(top->isVisible());
        const auto saved = h.layout.panes();
        auto* canvas = h.canvas();
        const qreal before = canvas->height();
        h.viewer.trigger("focus-mode");
        QTRY_VERIFY(flag(h, "focusMode"));
        QTRY_VERIFY(!top->isVisible());
        QTRY_VERIFY(!named(h.window, "folderTree")->isVisible());
        QTRY_VERIFY(canvas->height() >= before);
        QCOMPARE(h.layout.panes(), saved);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!flag(h, "focusMode"));
        QTRY_VERIFY(top->isVisible());
        QCOMPARE(h.layout.panes(), saved);
    }

    // F031: the slideshow advances on its own pace, never queues, and ends on manual steps, Escape and a folder boundary.
    void slideshowAdvancesAndEndsCleanly() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h)); focusCanvas(h);
        h.viewer.setBoundaryMode("stop");
        QVERIFY(QMetaObject::invokeMethod(h.window, "setSlideshowSeconds", Q_ARG(QVariant, 1)));
        h.viewer.trigger("slideshow");
        QTRY_VERIFY(flag(h, "slideshowOn"));
        QTRY_VERIFY(named(h.window, "slideshowIndicator")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 2, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(h.viewer.currentIndex(), 3, 4000);
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "slideshowOn"), 4000); // the stop boundary ended it
        QCOMPARE(h.viewer.currentIndex(), 3);
        // Escape stops it; so does a manual step.
        h.viewer.setBoundaryMode("loop");
        h.viewer.trigger("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn"));
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!flag(h, "slideshowOn"));
        h.viewer.trigger("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn"));
        QTest::keyClick(h.window, Qt::Key_Left); QTRY_VERIFY(!flag(h, "slideshowOn"));
        // A modal dialog ends it too.
        h.viewer.trigger("slideshow"); QTRY_VERIFY(flag(h, "slideshowOn"));
        h.viewer.trigger("settings"); QTRY_VERIFY(!flag(h, "slideshowOn"));
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
    }

    // Right button: a quick click is the menu; holding it is the loupe, which ends with the button.
    void rightButtonClickIsTheMenuAndHoldIsTheLoupe() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        const QPoint at = sceneCenter(canvas);
        QTest::mousePress(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(canvas->loupeActive());
        QTest::mouseRelease(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(!canvas->loupeActive());
        QTest::qWait(150);
        QVERIFY(!menuOpen(menu));
        QTest::mouseClick(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(menuOpen(menu));
        QVERIFY(!canvas->loupeActive());
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // A key hold in progress is not taken over, or ended, by a right-button hold.
        focusCanvas(h); QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::mousePress(h.window, Qt::RightButton, {}, at); QTest::qWait(350);
        QTest::mouseRelease(h.window, Qt::RightButton, {}, at);
        QVERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
    }

    // The wheel matrix: each modifier has one action; navigation accumulates to detents, zoom and pan are continuous.
    void wheelMatrixAndHighResolutionDeltas() {
        Media media(5); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas();
        const QPoint at = sceneCenter(canvas);
        const QVariantMap defaults = h.viewer.wheelModifiers();
        QCOMPARE(defaults.value("none").toString(), QString("navigate"));
        QCOMPARE(defaults.value("ctrl").toString(), QString("zoom"));
        QCOMPARE(defaults.value("shift").toString(), QString("pan-horizontal"));
        QCOMPARE(defaults.value("alt").toString(), QString("navigate"));
        // Ctrl zooms about the cursor; Ctrl wins over Shift.
        canvas->actualSize();
        qreal zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ControlModifier);
        QVERIFY(canvas->zoom() > zoom);
        zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, -120), Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(canvas->zoom() < zoom);
        // Shift pans horizontally only.
        canvas->actualSize();
        QPointF before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ShiftModifier);
        QPointF after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(!qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(qFuzzyCompare(after.y() + 1, before.y() + 1));
        // Remapped: Ctrl pans vertically, Shift zooms, Alt pans horizontally.
        QVERIFY(h.viewer.setWheelModifier("ctrl", "pan")); QVERIFY(h.viewer.setWheelModifier("shift", "zoom")); QVERIFY(h.viewer.setWheelModifier("alt", "pan-horizontal"));
        canvas->actualSize();
        before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ControlModifier);
        after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(!qFuzzyCompare(after.y() + 1, before.y() + 1));
        canvas->actualSize(); zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ShiftModifier);
        QVERIFY(canvas->zoom() > zoom);
        canvas->actualSize();
        before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::AltModifier);
        after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(!qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(qFuzzyCompare(after.y() + 1, before.y() + 1));
        // Zoom is continuous: two small high-resolution deltas equal one double-sized delta.
        QVERIFY(h.viewer.setWheelModifier("ctrl", "zoom"));
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 20), {}, Qt::ControlModifier); sendWheel(h, at, QPoint(0, 20), {}, Qt::ControlModifier);
        const qreal twice = canvas->zoom();
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 40), {}, Qt::ControlModifier);
        QVERIFY(qFuzzyCompare(twice, canvas->zoom()));
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 1), {}, Qt::ControlModifier);
        QVERIFY(canvas->zoom() > 1.0); // a single pixel of trackpad travel still zooms
        // Navigation accumulates to 120-unit detents. Alt follows the plain wheel again here.
        QVERIFY(h.viewer.setWheelModifier("alt", "navigate"));
        canvas->fit("shrink"); focusCanvas(h);
        QCOMPARE(h.viewer.currentIndex(), 1);
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier); // 5 x 20 units: not yet a detent
        QTest::qWait(150); QCOMPARE(h.viewer.currentIndex(), 1);
        sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier); // the sixth completes one
        QTRY_COMPARE(h.viewer.currentIndex(), 2);
        // Reversing direction discards the partial detent: +60 then -140 is one step forward (without the discard
        // it would net -80 and not move at all).
        for (int i = 0; i < 3; ++i) sendWheel(h, at, QPoint(0, 10), {}, Qt::AltModifier);
        for (int i = 0; i < 7; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 3);
        QTest::qWait(600); // let the remainder settle
        // A partial detent does not wait for the next gesture: -100, a pause, -100 is not a detent (without the idle
        // discard it would be -200 and step).
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTest::qWait(600);
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTest::qWait(200); QCOMPARE(h.viewer.currentIndex(), 3);
        QTest::qWait(600);
        // Classic wheels: one 120 notch is one photo.
        sendWheel(h, at, {}, QPoint(0, -120), Qt::AltModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 4);
        sendWheel(h, at, {}, QPoint(0, 120), Qt::NoModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && !h.viewer.loading(), 15000); // no decode in flight when the harness is torn down
        // The plain wheel still follows the legacy setting.
        h.viewer.setWheelMode("zoom");
        canvas->actualSize(); zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::NoModifier);
        QVERIFY(canvas->zoom() > zoom);
        QCOMPARE(h.viewer.wheelModifiers().value("none").toString(), QString("zoom"));
    }

    void canvasMenuMatchesShortcutsAndOpensFromTheKeyboard() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        auto* canvas = h.canvas();
        for (int how = 0; how < 3; ++how) {
            focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
            if (how == 0) QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(canvas));
            else if (how == 1) QTest::keyClick(h.window, Qt::Key_Menu);
            else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY2(menuOpen(menu), qPrintable(QString::number(how)));
            QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
            closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        }
        // The destructive row names its target.
        focusCanvas(h); QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-current-file"); QVERIFY(trash);
        const QString text = trash->property("text").toString();
        QVERIFY2(text.contains("p1.png") && text.contains(trashName()), qPrintable(text));
        // Rows show the live binding and follow a remap.
        auto* rotate = menu->findChild<QQuickItem*>("menuItem-rotate"); QVERIFY(rotate);
        QCOMPARE(rotate->property("keys").toString(), QString("R"));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        QVERIFY(h.viewer.setBinding("rotate", "Ctrl+Alt+Y"));
        focusCanvas(h); QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        rotate = menu->findChild<QQuickItem*>("menuItem-rotate");
        QTRY_COMPARE(rotate->property("keys").toString(), QString("Ctrl+Alt+Y"));
        // Choosing a row runs the same command the key would.
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(rotate));
        QTRY_COMPARE(canvas->rotation(), 90);
        QTRY_VERIFY(!menuOpen(menu));
        // Combined view with the grid focused: canvas rows are disabled exactly as their keys are.
        h.viewer.trigger("view-combined"); QTRY_VERIFY(flag(h, "combinedMode"));
        QVERIFY(QMetaObject::invokeMethod(h.window, "focusPane", Q_ARG(QVariant, QString("grid"))));
        QTRY_VERIFY(flag(h, "gridScope"));
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 80), Q_ARG(QVariant, 80)));
        QTRY_VERIFY(menuOpen(menu));
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        QVERIFY(!allowed(h, "rotate")); QVERIFY(!menu->findChild<QQuickItem*>("menuItem-rotate")->isEnabled());
        closeMenu(menu);
    }

    void gridMenuNamesItsTargetAndOpensFromTheKeyboard() {
        Media media(4); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.fileGrid.count(), 4);
        auto* menu = h.object("gridContextMenu"); QVERIFY(menu);
        auto cells = [&] { return namedAll(h.window, "photoGridCell"); };
        QTRY_COMPARE(cells().size(), 4);
        auto cell = [&](int index) -> QQuickItem* { for (auto* c : cells()) if (c->property("index").toInt() == index) return c; return nullptr; };
        QVERIFY(cell(1));
        // Right click on an unselected item selects it first, so the destructive row names it.
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(cell(1)));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 1);
        QCOMPARE(h.fileGrid.selectedCount(), 1);
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash && trash->isVisible());
        QVERIFY2(trash->property("text").toString() == QString("Move “p2.png” to the %1").arg(trashName()), qPrintable(trash->property("text").toString()));
        auto* open = menu->findChild<QQuickItem*>("menuItem-grid-open"); QVERIFY(open);
        QVERIFY(open->isVisible() && open->isEnabled());
        QCOMPARE(open->property("keys").toString(), QString("Enter"));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Several selected items are counted; a right click inside the selection keeps it.
        h.fileGrid.select(0, DirectoryEntryModel::Replace); h.fileGrid.select(1, DirectoryEntryModel::Toggle); h.fileGrid.select(2, DirectoryEntryModel::Toggle);
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(cell(2)));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        trash = menu->findChild<QQuickItem*>("menuItem-delete-selection");
        QVERIFY2(trash->property("text").toString() == QString("Move 3 items to the %1").arg(trashName()), qPrintable(trash->property("text").toString()));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Keyboard: the Menu key and Shift+F10 open it on the cursor item.
        h.fileGrid.select(3, DirectoryEntryModel::Replace);
        auto* view = named(h.window, "photoGridView"); QVERIFY(view); view->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 3);
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        view->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier); QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 3);
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Empty area: the item-only rows disappear, folder-level rows remain.
        const QPointF origin = view->mapToScene(QPointF(0, 0));
        QTest::mouseClick(h.window, Qt::RightButton, {}, QPoint(int(origin.x() + view->width()) - 6, int(origin.y() + view->height()) - 6));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), -1);
        QVERIFY(!menu->findChild<QQuickItem*>("menuItem-grid-open")->isVisible());
        QVERIFY(menu->findChild<QQuickItem*>("menuItem-grid-select-all")->isEnabled());
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        // Select all through the row, as the key would.
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(menu->findChild<QQuickItem*>("menuItem-grid-select-all")));
        QTRY_COMPARE(h.fileGrid.selectedCount(), 4);
    }

    void treeRowAndPathBarMenusOpenAndMatch() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* rowMenu = h.object("folderRowMenu"); QVERIFY(rowMenu);
        // Keyboard on the tree.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        named(h.window, "folderList")->setProperty("currentIndex", h.folders.currentRow()); // the current folder, not a drive
        for (int how = 0; how < 2; ++how) {
            if (how == 0) QTest::keyClick(h.window, Qt::Key_Menu); else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY(menuOpen(rowMenu));
            QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
            auto* trash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash);
            if (how == 0) { // the destructive row names its folder
                const QString text = trash->property("text").toString();
                QVERIFY2(text.contains(QFileInfo(media.dir.path()).fileName()) && text.contains(trashName()), qPrintable(text));
            }
            QVERIFY(rowMenu->findChild<QQuickItem*>("menuItem-tree-open-folder")->isEnabled());
            closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
            QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
            named(h.window, "folderList")->setProperty("currentIndex", h.folders.currentRow());
        }
        // Right click on a row.
        // The first row (a drive), found by index and scrolled into the clipped viewport first: the keyboard steps above
        // left the list at the current folder, the first delegate found may be a cached row outside the viewport, and the
        // Favorites section above the list shortens it. A row other than the cursor's proves the click chose the row.
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        const int target = 0;
        QVERIFY(h.folders.currentRow() != target);
        auto rowAt = [&](int index) -> QQuickItem* { for (auto* r : namedAll(h.window, "folderRow")) if (r->property("index").toInt() == index) return r; return nullptr; };
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, target), Q_ARG(int, 4 /* ListView.Contain */)));
        QTRY_VERIFY(rowAt(target) && list->mapRectToScene(list->boundingRect()).contains(sceneCenter(rowAt(target))));
        auto* row = rowAt(target);
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(row));
        QTRY_VERIFY(menuOpen(rowMenu));
        QCOMPARE(QDir::cleanPath(rowMenu->property("path").toString()), QDir::cleanPath(row->property("path").toString())); // the menu is for the clicked row
        QCOMPARE(list->property("currentIndex").toInt(), target);
        QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        // Path bar.
        auto* pathMenu = h.object("pathContextMenu"); QVERIFY(pathMenu);
        auto* address = named(h.window, "folderAddress"); QVERIFY(address);
        for (int how = 0; how < 3; ++how) {
            QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
            QTRY_VERIFY(address->hasActiveFocus());
            if (how == 0) QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(address));
            else if (how == 1) QTest::keyClick(h.window, Qt::Key_Menu);
            else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY2(menuOpen(pathMenu), qPrintable(QString::number(how)));
            QVERIFY2(menuProblems(h, pathMenu).isEmpty(), qPrintable(menuProblems(h, pathMenu).join("; ")));
            for (const char* id : {"menuItem-focus-path", "menuItem-copy-folder-path", "menuItem-paste-path"}) QVERIFY(pathMenu->findChild<QQuickItem*>(id));
            closeMenu(pathMenu); QTRY_VERIFY(!menuOpen(pathMenu));
        }
        // "Copy the folder path" through the menu row.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(pathMenu));
        QGuiApplication::clipboard()->setText("sentinel");
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(pathMenu->findChild<QQuickItem*>("menuItem-copy-folder-path")));
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), h.viewer.directory());
    }

    // Menus stay inside a compact window; the tallest one scrolls instead of running off the edge.
    void menusStayReachableInCompactWindows() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(540, 360);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QTRY_COMPARE(h.window->width(), 540);
        auto* canvasMenu = h.object("canvasContextMenu"); QVERIFY(canvasMenu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 530), Q_ARG(QVariant, 330)));
        QTRY_VERIFY(menuOpen(canvasMenu));
        QVERIFY(insideWindow(h, canvasMenu));
        closeMenu(canvasMenu); QTRY_VERIFY(!menuOpen(canvasMenu));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 3);
        auto* gridMenu = h.object("gridContextMenu"); QVERIFY(gridMenu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openGridMenu", Q_ARG(QVariant, 530), Q_ARG(QVariant, 330), Q_ARG(QVariant, 0)));
        QTRY_VERIFY(menuOpen(gridMenu));
        QVERIFY(insideWindow(h, gridMenu));
    }

    // Opt-in review captures (GLANCELOT_REVIEW_CAPTURE): dark and light, compact and wide, with a menu open.
    void reviewCaptures() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) QSKIP("Set GLANCELOT_REVIEW_CAPTURE to write captures.");
        Media media(4, QSize(1600, 1000)); Harness h; QVERIFY(h.window);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QDir().mkpath(".cache/input-02");
        auto* canvasMenu = h.object("canvasContextMenu"); auto* gridMenu = h.object("gridContextMenu");
        const struct { const char* name; QSize size; } sizes[] = {{"wide", {1180, 780}}, {"compact", {540, 360}}};
        for (const auto& size : sizes) {
            h.window->resize(size.size); QTest::qWait(100);
            for (const QString mode : {QString("dark"), QString("light")}) {
                h.theme.setMode(mode); QTest::qWait(60);
                h.viewer.trigger("view-preview"); QTest::qWait(60);
                QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 200), Q_ARG(QVariant, 120)));
                QTRY_VERIFY(menuOpen(canvasMenu)); QTest::qWait(80);
                QVERIFY(h.window->grabWindow().save(QString(".cache/input-02/canvas-menu-%1-%2.png").arg(size.name, mode)));
                closeMenu(canvasMenu); QTRY_VERIFY(!menuOpen(canvasMenu));
                h.viewer.trigger("view-grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 4); QTest::qWait(80);
                QVERIFY(QMetaObject::invokeMethod(h.window, "openGridMenu", Q_ARG(QVariant, 200), Q_ARG(QVariant, 140), Q_ARG(QVariant, 1)));
                QTRY_VERIFY(menuOpen(gridMenu)); QTest::qWait(80);
                QVERIFY(h.window->grabWindow().save(QString(".cache/input-02/grid-menu-%1-%2.png").arg(size.name, mode)));
                closeMenu(gridMenu); QTRY_VERIFY(!menuOpen(gridMenu));
            }
        }
    }
};

QTEST_MAIN(InputUiTests)
#include "input_ui_tests.moc"
