#include "ui_test_support.hpp"
#include "perf_enum_test_support.hpp"
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QDir>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>

// A1 F016 in the shipped ui/Main.qml. The folder holds one real photo so the viewer can open it; the seam supplies the
// grid's rows, arriving backwards so the definitive order really moves every row.
namespace {
constexpr int kRows = 3000, kHeldAfter = 1024, kPage = 1000;
struct ArrivalFixture {
    Media media{1, QSize(64, 48)};
    FakeFolder folder;
    int photoAt{2500}; // arrival position of the real photo's row
    int heldAfter{kHeldAfter}, batchRows{64};
    std::function<QString(int)> customName;
    std::shared_ptr<std::atomic<std::uint64_t>> freeMemory; // what the memory check reads, changeable mid-test
    FakeFolder holding;                                       // the next listing, held partway, when holdNext is set
    std::shared_ptr<std::atomic_bool> holdNext = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<SeamScope> seam;
    std::unique_ptr<Harness> h;
    bool start() {
        folder.total = kRows; folder.heldAfter = heldAfter;
        folder.name = [photo = photoAt](int i) { return i == photo ? QString("p1.png") : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0')); };
        if (customName) folder.name = customName;
        seam = std::make_unique<SeamScope>(&folder, batchRows);
        h = std::make_unique<Harness>();
        if (!h->window) return false;
        h->window->resize(1180, 780);
        h->viewer.setFilmstripVisible(false);
        h->viewer.open(media.path(0));
        h->viewer.trigger("grid");
        h->window->requestActivate();
        return QTest::qWaitFor([&] { return h->viewer.imageReady() && h->fileGrid.count() == heldAfter && h->fileGrid.loading() && view() && view()->property("count").toInt() == heldAfter; }, 15000);
    }
    // Continuation: listed in pages of 1000 up to 3000, each page landing above the rows already showing.
    bool startPaged(int total) {
        folder.total = total;
        folder.name = [photo = photoAt, total](int i) { return i == photo ? QString("p1.png") : QString("f%1.txt").arg(total - i, 5, 10, QChar('0')); };
        if (customName) folder.name = customName;
        holding.total = total; holding.name = folder.name; holding.heldAfter = kPage / 2;
        seam = std::make_unique<SeamScope>(&folder);
        directory_scan::testSeam.enumerator = [plain = folder.enumerator(), held = holding.enumerator(), hold = holdNext](
            const std::filesystem::path& directory, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return (hold->load() ? held : plain)(directory, hidden, cancel, limit, sink);
        };
        directory_scan::testSeam.tuning.entryStep = kPage; directory_scan::testSeam.tuning.entryLimit = 3000;
        if (freeMemory) directory_scan::testSeam.availableMemory = [spare = freeMemory]() -> std::optional<std::uint64_t> { return spare->load(); };
        h = std::make_unique<Harness>();
        if (!h->window) return false;
        h->window->resize(1180, 780);
        h->viewer.setFilmstripVisible(false);
        h->viewer.open(media.path(0));
        h->viewer.trigger("grid");
        h->window->requestActivate();
        return QTest::qWaitFor([&] { return h->viewer.imageReady() && h->fileGrid.count() == kPage && !h->fileGrid.loading() && view() && view()->property("count").toInt() == kPage; }, 15000)
            && QTest::qWaitFor([&] { return h->window->isExposed(); }, 10000);
    }
    QQuickItem* showMore() const { return named(h->window, "gridShowMore"); }
    QQuickItem* banner() const { return named(h->window, "gridTruncated"); }
    QString bannerText() const { return named(h->window, "gridTruncatedText")->property("text").toString(); }
    bool listed(int rows) const { return QTest::qWaitFor([&] { return !h->fileGrid.loading() && h->fileGrid.count() == rows; }, 10000); }
    bool tabToShowMore() const {
        for (int i = 0; i < 16 && !showMore()->hasActiveFocus(); ++i) QTest::keyClick(h->window, Qt::Key_Tab);
        return showMore()->hasActiveFocus();
    }
    QQuickItem* view() const { return h->window->findChild<QQuickItem*>("photoGridView"); }
    qreal contentY() const { return view()->property("contentY").toReal(); }
    int rowAtViewTop() const {
        int row = -1;
        QMetaObject::invokeMethod(view(), "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, view()->property("contentX").toReal() + 4), Q_ARG(qreal, contentY() + 4));
        return row;
    }
    QString countText() const { return named(h->window, "gridItemCount")->property("text").toString(); }
    bool settled() const { return !h->fileGrid.loading() && h->fileGrid.count() == kRows; }
    QQuickItem* photoGrid() const { return h->window->findChild<QQuickItem*>("photoGrid"); }
    bool placing() const { return photoGrid()->property("placeCursor").toBool(); }
    int columns() const { return photoGrid()->property("columns").toInt(); }
    void scrollDown(int rows) { view()->setProperty("contentY", view()->property("originY").toReal() + view()->property("cellHeight").toReal() * (rows / columns())); }
    bool anchoredOn(const QString& path) const { const int top = rowAtViewTop(), row = h->fileGrid.rowOf(path); return top >= 0 && row >= 0 && row / columns() == top / columns(); }
    bool readyForInput() { return QTest::qWaitFor([&] { return h->window->isExposed(); }, 10000) && activate(*h); }
};
// Runs the placements waiting behind the last signal, which Qt.callLater has queued.
void runPlacements() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
// A folder browsed to row 1500, whose listing can then fail, succeed, or succeed and hold partway.
struct RetryFixture {
    enum Source { Lists, Fails, FailsPartway, ListsAndHolds };
    Media media{1, QSize(64, 48)};
    std::shared_ptr<std::atomic_int> source = std::make_shared<std::atomic_int>(Lists);
    FakeFolder listing, failing, failingPartway, holding;
    SeamScope seam{nullptr};
    std::unique_ptr<Harness> h;
    QString anchor;
    bool start(bool orderMoves) {
        // With the order unchanged the photo arrives last, where it sorts, and no layout change fires.
        const auto names = [orderMoves](int i) {
            if (i == (orderMoves ? 2500 : kRows - 1)) return QString("p1.png");
            return QString("f%1.txt").arg(orderMoves ? kRows - i : i, 5, 10, QChar('0'));
        };
        listing.total = holding.total = failing.total = failingPartway.total = kRows;
        listing.name = holding.name = failingPartway.name = names;
        failing.failAfter = 0; failingPartway.failAfter = kHeldAfter; holding.heldAfter = kHeldAfter;
        const auto lists = listing.enumerator(), fails = failing.enumerator(), failsPartway = failingPartway.enumerator(), holds = holding.enumerator();
        directory_scan::testSeam.enumerator = [=, which = source](const std::filesystem::path& d, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            const auto& chosen = which->load() == Fails ? fails : which->load() == FailsPartway ? failsPartway : which->load() == ListsAndHolds ? holds : lists;
            return chosen(d, hidden, cancel, limit, sink);
        };
        h = std::make_unique<Harness>();
        if (!h->window) return false;
        h->window->resize(1180, 780);
        h->viewer.setFilmstripVisible(false);
        h->viewer.open(media.path(0));
        h->viewer.trigger("grid");
        if (!QTest::qWaitFor([&] { return !h->fileGrid.loading() && h->fileGrid.count() == kRows && h->viewer.imageReady() && !grid()->property("placeCursor").toBool(); }, 15000)) return false;
        runPlacements();
        QMetaObject::invokeMethod(view(), "positionViewAtIndex", Q_ARG(int, 1500), Q_ARG(int, 1));
        if (!QTest::qWaitFor([&] { return top() >= 1400; }, 10000)) return false;
        anchor = h->fileGrid.pathAt(top());
        return true;
    }
    bool fail(Source how = Fails) {
        source->store(how);
        h->fileGrid.refresh();
        const bool failed = QTest::qWaitFor([&] { return !h->fileGrid.loading() && !h->fileGrid.accessible() && h->fileGrid.count() == 0; }, 10000);
        runPlacements();
        return failed;
    }
    bool recovered() { return QTest::qWaitFor([&] { return !h->fileGrid.loading() && h->fileGrid.accessible() && h->fileGrid.count() == kRows; }, 10000); }
    QQuickItem* grid() const { return h->window->findChild<QQuickItem*>("photoGrid"); }
    QQuickItem* view() const { return h->window->findChild<QQuickItem*>("photoGridView"); }
    int columns() const { return grid()->property("columns").toInt(); }
    int top() const {
        int row = -1;
        QMetaObject::invokeMethod(view(), "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, view()->property("contentX").toReal() + 4), Q_ARG(qreal, view()->property("contentY").toReal() + 4));
        return row;
    }
    bool anchoredOn(const QString& path) const { const int row = h->fileGrid.rowOf(path), at = top(); return at >= 0 && row >= 0 && row / columns() == at / columns(); }
};
// A real folder whose identity probes wait for release: the window between the definitive order and the baseline.
struct ProbeWindow {
    std::shared_ptr<std::atomic_bool> hold = std::make_shared<std::atomic_bool>(false), released = std::make_shared<std::atomic_bool>(false);
    SeamScope seam{nullptr};
    ProbeWindow() {
        directory_scan::testSeam.tuning = {};
        directory_scan::testSeam.beforeProbes = [hold = hold, released = released](std::stop_token stop) {
            if (!hold->load()) return;
            while (!released->load() && !stop.stop_requested()) std::this_thread::yield();
        };
    }
};
QQuickItem* descendant(QQuickItem* root, const QString& name) { // delegates are visual, not QObject, children
    QList<QQuickItem*> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item->objectName() == name) return item;
        pending << item->childItems();
    }
    return nullptr;
}
QQuickItem* cellForRow(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}
}

class PerfEnumUiTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid()); qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    // Left alone, the view stays on the arriving rows, labelled as partial, then lands on the viewed photo.
    void untouchedArrivalIsLabelledThenRevealsTheCurrentPhoto() {
        ArrivalFixture f; QVERIFY(f.start());
        QTRY_VERIFY_WITH_TIMEOUT(f.countText().contains("so far"), 10000);
        QCOMPARE(f.h->fileGrid.cursorRow(), -1); // nothing placed on a provisional order
        QSignalSpy resets(&f.h->fileGrid, &QAbstractItemModel::modelReset);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        QCOMPARE(resets.count(), 0);
        QVERIFY(!f.countText().contains("so far"));
        const int row = f.h->fileGrid.rowOf(f.media.path(0));
        QCOMPARE(f.h->fileGrid.cursorRow(), row);
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), row) != nullptr, 10000); // positioned on it
    }
    // The photo being viewed is placed as soon as its row arrives, and stays in view through the definitive order.
    void photoArrivingEarlyIsPlacedAndKeptInView() {
        ArrivalFixture f;
        f.photoAt = 300;
        QVERIFY(f.start());
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        QVERIFY(f.h->fileGrid.arriving());
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), f.h->fileGrid.cursorRow()) != nullptr, 10000);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), f.h->fileGrid.rowOf(f.media.path(0))) != nullptr, 10000);
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)});
    }
    // A delegate that keeps its place through the new order shows its new row's thumbnail, not the old row's.
    void definitiveOrderRebindsDelegatesInPlace() {
        ArrivalFixture f; QVERIFY(f.start());
        QMetaObject::invokeMethod(f.view(), "movementStarted"); // the user took over, so nothing moves the view off the top
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), 0) != nullptr, 10000);
        QQuickItem* firstCell = cellForRow(f.view(), 0);
        const QString before = f.h->fileGrid.pathAt(0);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QVERIFY(f.h->fileGrid.pathAt(0) != before);
        QCOMPARE(cellForRow(f.view(), 0), firstCell); // the same delegate, on a different file
        QCOMPARE(firstCell->property("path").toString(), f.h->fileGrid.pathAt(0));
        QQuickItem* thumbnail = descendant(firstCell, "gridThumb0");
        QVERIFY(thumbnail);
        const int edge = f.h->window->findChild<QQuickItem*>("photoGrid")->property("thumbEdge").toInt();
        QTRY_COMPARE_WITH_TIMEOUT(thumbnail->property("source").toUrl().toString(), f.h->fileGrid.thumbnailSource(0, edge), 10000);
    }
    // A row clicked during arrival is the user's choice: the placement that was waiting does not replace it.
    void clickDuringArrivalSurvivesTheDefinitiveOrder() {
        ArrivalFixture f; QVERIFY(f.start());
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), 6) != nullptr && cellForRow(f.view(), 6)->height() > 0, 10000);
        QQuickItem* cell = cellForRow(f.view(), 6);
        const QString chosen = f.h->fileGrid.pathAt(6);
        QVERIFY(QTest::qWaitFor([&] { return f.h->window->isExposed(); }, 10000));
        QTest::mouseClick(f.h->window, Qt::LeftButton, Qt::NoModifier, sceneCenter(cell));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{chosen}, 10000);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.cursorRow(), f.h->fileGrid.rowOf(chosen), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), f.h->fileGrid.rowOf(chosen)) != nullptr, 10000);
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{chosen});
    }
    // "Show more" from the keyboard: reachable, named, and the photo the user is on stays selected and in view.
    void showMoreFromTheKeyboardKeepsThePhotoInView() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        const int before = f.h->fileGrid.cursorRow();
        QVERIFY(f.banner()->isVisible()); QVERIFY(f.showMore()->isVisible());
        QVERIFY(f.bannerText().contains(QLocale().toString(kPage)));
        QAccessible::setActive(true);
        QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(f.showMore());
        QVERIFY(accessible); QCOMPARE(accessible->text(QAccessible::Name), QString("Show more items in this folder"));
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QVERIFY(f.h->fileGrid.loading());
        QVERIFY(f.bannerText().contains("Listing more"));
        QTest::keyClick(f.h->window, Qt::Key_Return); // a second press changes nothing
        QVERIFY(f.listed(2000));
        const int row = f.h->fileGrid.rowOf(f.media.path(0));
        QVERIFY(row != before);
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)});
        QCOMPARE(f.h->fileGrid.cursorRow(), row);
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), row) != nullptr, 10000);
        QQuickItem* cell = cellForRow(f.view(), row);
        const auto inView = [&] {
            const qreal y = cell->y() - f.contentY();
            return y >= -1 && y + cell->height() <= f.view()->height() + 1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(inView(), 10000);
        QVERIFY(f.showMore()->hasActiveFocus()); // focus stays for the next press
        QVERIFY(f.bannerText().contains(QLocale().toString(2000)));
    }
    // Browsing elsewhere with the wheel: the row at the top stays at the top, wherever the new page moved it.
    void showMoreKeepsTheVisibleRowWhileBrowsing() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        const int columns = f.h->window->findChild<QQuickItem*>("photoGrid")->property("columns").toInt();
        f.view()->setProperty("contentY", f.view()->property("originY").toReal() + f.view()->property("cellHeight").toReal() * (300 / columns));
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        const int rowBefore = f.rowAtViewTop();
        QTest::mouseClick(f.h->window, Qt::LeftButton, Qt::NoModifier, sceneCenter(f.showMore()));
        QVERIFY(f.listed(2000));
        QVERIFY(f.h->fileGrid.rowOf(anchor) != rowBefore);
        const auto anchored = [&] { const int top = f.rowAtViewTop(), row = f.h->fileGrid.rowOf(anchor); return top >= 0 && row / columns == top / columns; };
        QTRY_VERIFY_WITH_TIMEOUT(anchored(), 10000);
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)});
    }
    void aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner() {
        // The sort command saves the order, and the tests after this one must not start from it.
        QTemporaryDir ownConfig; QVERIFY(ownConfig.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", ownConfig.path().toUtf8());
        const auto sharedConfig = qScopeGuard([this] { qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8()); });
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        const int columns = f.columns();
        f.scrollDown(600 / columns);
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        const int rowBefore = f.h->fileGrid.rowOf(anchor);
        f.h->viewer.trigger("sort-reverse"); // what the menu item and the shortcut do
        QTRY_VERIFY_WITH_TIMEOUT(!f.h->fileGrid.loading() && f.h->fileGrid.rowOf(anchor) != rowBefore, 10000);
        QCOMPARE(f.h->fileGrid.count(), kPage);
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(anchor), 10000);
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)});
        QVERIFY(f.banner()->isVisible());
        QVERIFY(f.bannerText().contains("This folder has more that are not listed here"));
        QVERIFY(f.showMore()->isVisible());
    }
    // B2: refuse, free memory, press again, it lists. Through the real button and keys.
    void notEnoughMemorySaysWhatToDoAndTheNextPressChecksAgain() {
        ArrivalFixture f; f.photoAt = 300; f.freeMemory = std::make_shared<std::atomic<std::uint64_t>>(1024); QVERIFY(f.startPaged(2500));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("not enough free memory"), 10000);
        QVERIFY(f.bannerText().contains("Close other apps, then try again"));
        QVERIFY(f.bannerText().contains(QLocale().toString(kPage)));
        QVERIFY(!f.bannerText().contains("Listing more"));
        QVERIFY(!f.h->fileGrid.loading());
        QCOMPARE(f.h->fileGrid.count(), kPage);
        QVERIFY(f.showMore()->isVisible()); QVERIFY(f.showMore()->isEnabled());
        QVERIFY(f.showMore()->hasActiveFocus()); // no focus to rescue: the button stays
        f.h->fileGrid.setFilter("f0", true);
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("not enough free memory") && f.bannerText().contains("some matches may be missing"), 10000);
        f.h->fileGrid.clearFilter();
        f.freeMemory->store(directory_scan::kMemoryHeadroomBytes * 4); // other apps closed
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QVERIFY(f.listed(2000));
        QTRY_VERIFY_WITH_TIMEOUT(!f.bannerText().contains("not enough free memory"), 10000);
    }
    // N6: a press refused while a refresh runs says why, not "Listing more items…".
    void aRefusedPressDuringARefreshSaysWhy() {
        ArrivalFixture f; f.photoAt = 300; f.freeMemory = std::make_shared<std::atomic<std::uint64_t>>(1024); QVERIFY(f.startPaged(2500));
        f.holdNext->store(true);
        f.h->fileGrid.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(f.h->fileGrid.loading() && f.holding.starts->load() == 1, 10000);
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("not enough free memory"), 10000);
        QVERIFY(!f.photoGrid()->property("moreRequested").toBool());
        f.holding.release();
        QTRY_VERIFY_WITH_TIMEOUT(!f.h->fileGrid.loading(), 10000);
        QVERIFY(f.bannerText().contains("not enough free memory"));
    }
    // N4: nothing offers a page while the folder re-arrives, and nothing says one is running.
    void showMoreIsNotOfferedWhileTheFolderReArrives() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        f.h->viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!f.photoGrid()->isVisible(), 10000);
        f.holdNext->store(true);
        f.h->viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(f.h->fileGrid.arriving() && f.h->fileGrid.count() > 0, 10000); // held partway, published a batch at a time
        QVERIFY(!f.showMore()->isVisible());
        QVERIFY(!f.bannerText().contains("Listing more"));
        f.holding.release();
        QTRY_VERIFY_WITH_TIMEOUT(!f.h->fileGrid.loading() && f.h->fileGrid.count() == kPage, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(f.showMore()->isVisible(), 10000);
    }
    // N5: a page of the folder before says nothing about the next one.
    void anotherFolderDuringAPageDoesNotSayListingMore() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        f.holdNext->store(true);
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("Listing more"), 10000);
        Media other{1, QSize(64, 48)};
        f.h->viewer.navigateDirectory(other.dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(file_grid_wiring::directoryKey(f.h->fileGrid.directory()) == file_grid_wiring::directoryKey(other.dir.path()), 10000);
        QVERIFY(!f.photoGrid()->property("moreRequested").toBool());
        f.holding.release();
    }
    // N3: a sort of a truncated folder re-lists it, which takes seconds at a million rows. The banner says so.
    void aSortOfATruncatedFolderSaysItIsSorting() {
        QTemporaryDir ownConfig; QVERIFY(ownConfig.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", ownConfig.path().toUtf8());
        const auto sharedConfig = qScopeGuard([this] { qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8()); });
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        f.holdNext->store(true);
        f.h->viewer.trigger("sort-reverse");
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText() == QString("Sorting the %1 items listed here…").arg(QLocale().toString(kPage)), 10000);
        f.holding.release();
        QTRY_VERIFY_WITH_TIMEOUT(!f.h->fileGrid.loading(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("This folder has more that are not listed here"), 10000);
    }
    // B1 through the shipped QML: sort in the photo view, come back, and the cursor, Enter, a Shift range and the next
    // refresh all act on the rows the user sees.
    void aSortInThePhotoViewKeepsTheGridsRowsRight() {
        QTemporaryDir ownConfig; QVERIFY(ownConfig.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", ownConfig.path().toUtf8());
        const auto sharedConfig = qScopeGuard([this] { qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8()); });
        ArrivalFixture f; f.photoAt = 300;
        f.customName = [](int i) { return i == 300 ? QString("p1.png") : i % 10 == 0 ? QString("dir%1").arg(2500 - i, 5, 10, QChar('0')) : QString("f%1.txt").arg(2500 - i, 5, 10, QChar('0')); };
        QVERIFY(f.startPaged(2500));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
        f.h->viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!f.photoGrid()->isVisible(), 10000);
        f.h->viewer.trigger("sort-reverse");
        f.h->viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(f.photoGrid()->isVisible() && !f.h->fileGrid.loading() && f.h->fileGrid.count() == kPage && !f.placing(), 10000);
        runPlacements();
        int wrong = 0;
        for (int i = 0; i < f.h->fileGrid.count(); ++i) if (f.h->fileGrid.rowOf(f.h->fileGrid.pathAt(i)) != i) ++wrong;
        QCOMPARE(wrong, 0);
        QCOMPARE(f.h->fileGrid.pathAt(f.h->fileGrid.cursorRow()), f.media.path(0));
        QVERIFY(f.readyForInput());
        f.view()->forceActiveFocus();
        QTRY_VERIFY_WITH_TIMEOUT(f.view()->hasActiveFocus(), 10000);
        const int cursor = f.h->fileGrid.cursorRow();
        QTest::keyClick(f.h->window, Qt::Key_Right, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedCount(), 2, 10000);
        QCOMPARE(f.h->fileGrid.selectedPaths(), (QStringList{f.h->fileGrid.pathAt(cursor), f.h->fileGrid.pathAt(cursor + 1)}));
        int lowest = f.h->fileGrid.count();
        connect(&f.h->fileGrid, &DirectoryEntryModel::countChanged, this, [&] { lowest = std::min(lowest, f.h->fileGrid.count()); });
        f.h->fileGrid.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!f.h->fileGrid.loading(), 10000);
        QCOMPARE(lowest, kPage);
        QTest::keyClick(f.h->window, Qt::Key_Left); // back onto the photo
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.pathAt(f.h->fileGrid.cursorRow()), f.media.path(0), 10000);
        QTest::keyClick(f.h->window, Qt::Key_Return); // opens what is highlighted
        QTRY_VERIFY_WITH_TIMEOUT(!f.photoGrid()->isVisible(), 10000);
        QCOMPARE(f.h->viewer.filePath(), f.media.path(0));
    }
    void atTheLimitTheBannerSaysSoAndFocusGoesToTheGrid() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(5000));
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QVERIFY(f.listed(2000));
        QVERIFY(f.showMore()->hasActiveFocus());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QVERIFY(f.listed(3000));
        QTRY_VERIFY_WITH_TIMEOUT(!f.showMore()->isVisible(), 10000);
        QVERIFY(f.banner()->isVisible());
        QVERIFY(f.bannerText().contains("the most Glancelot can list"));
        QVERIFY(f.bannerText().contains(QLocale().toString(3000)));
        QTRY_VERIFY_WITH_TIMEOUT(f.view()->hasActiveFocus(), 10000);
        f.h->fileGrid.setFilter("f0", true); // a filtered count is not how much of the folder is listed
        QTRY_VERIFY_WITH_TIMEOUT(f.bannerText().contains("some matches may be missing"), 10000);
        QVERIFY(!f.bannerText().contains(QLocale().toString(3000)));
    }
    void everythingListedRemovesTheBannerAndFocusGoesToTheGrid() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(1500));
        QVERIFY(f.tabToShowMore());
        QTest::keyClick(f.h->window, Qt::Key_Return);
        QVERIFY(f.listed(1500));
        QTRY_VERIFY_WITH_TIMEOUT(!f.banner()->isVisible(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(f.view()->hasActiveFocus(), 10000);
    }
    // A filter hiding every listed row can still have matches further on, so the offer stays.
    void aFilterHidingEveryListedRowKeepsTheOffer() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        f.h->fileGrid.setFilter("nothing matches this", true);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.count(), 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(f.banner()->isVisible() && f.showMore()->isVisible(), 10000);
        QCOMPARE(f.bannerText(), QString("This folder has more items that are not listed here, so some matches may be missing."));
    }
    void bannerFitsAtNarrowAndWideWidths() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        QQuickItem* text = named(f.h->window, "gridTruncatedText");
        const QByteArray shots = qgetenv("GLANCELOT_PERF_ENUM_SHOTS");
        qreal narrow = 0;
        for (const int width : {560, 1180}) {
            f.h->window->resize(width, 780);
            QTRY_COMPARE_WITH_TIMEOUT(f.h->window->width(), width, 10000);
            if (width == 1180) QTRY_VERIFY_WITH_TIMEOUT(f.banner()->width() > narrow + 500, 10000); // the layout has grown
            const auto fits = [&] {
                const qreal button = f.showMore()->x();
                // In the window, not just in the banner: a layout that has not caught up with the resize is cut off.
                return f.showMore()->isVisible() && f.showMore()->mapToScene({f.showMore()->width(), 0}).x() <= width
                    && f.banner()->width() > 0 && text->x() + text->width() <= button && button + f.showMore()->width() <= f.banner()->width()
                    && f.showMore()->y() >= 0 && f.showMore()->y() + f.showMore()->height() <= f.banner()->height()
                    && text->y() >= 0 && text->y() + text->height() <= f.banner()->height();
            };
            QTRY_VERIFY2_WITH_TIMEOUT(fits(), qPrintable(QString("banner does not fit at %1").arg(width)), 10000);
            if (width == 560) narrow = f.banner()->width();
            QVERIFY(f.view()->mapToScene({}).y() >= f.banner()->mapToScene({}).y() + f.banner()->height());
            if (!shots.isEmpty()) {
                QVERIFY(QTest::qWaitFor([&] { return !f.h->window->grabWindow().isNull(); }, 10000));
                QVERIFY(f.h->window->grabWindow().save(QDir(QString::fromUtf8(shots)).filePath(QString("banner-%1.png").arg(width))));
            }
        }
    }
    // Scrolling during arrival anchors the view on the row at the top, through the definitive order.
    void scrollDuringArrivalKeepsTheVisibleRow() {
        ArrivalFixture f; QVERIFY(f.start());
        QMetaObject::invokeMethod(f.view(), "movementStarted"); // what a wheel or flick reports; offscreen wheels are not reliable here
        // Late in the arrival, so its row lands mid-list in the definitive order rather than clamped at either end.
        const int columns = f.h->window->findChild<QQuickItem*>("photoGrid")->property("columns").toInt();
        f.view()->setProperty("contentY", f.view()->property("originY").toReal() + f.view()->property("cellHeight").toReal() * (900 / columns));
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        QSignalSpy resets(&f.h->fileGrid, &QAbstractItemModel::modelReset);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        const auto anchored = [&] { const int top = f.rowAtViewTop(), row = f.h->fileGrid.rowOf(anchor); return top >= 0 && row / columns == top / columns; };
        QTRY_VERIFY_WITH_TIMEOUT(anchored(), 10000);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(f.h->fileGrid.cursorRow(), -1); // browsing with the wheel did not grow a cursor
    }
    // The view learns its new height in a frame. On a busy machine no frame runs between the last rows and the definitive
    // order, and placing the anchor against the old height left the view far above it. A hidden window has no frames.
    void definitiveOrderBeforeAnyFrameKeepsTheVisibleRow() {
        ArrivalFixture f; QVERIFY(f.start());
        QMetaObject::invokeMethod(f.view(), "movementStarted");
        const int columns = f.h->window->findChild<QQuickItem*>("photoGrid")->property("columns").toInt();
        f.view()->setProperty("contentY", f.view()->property("originY").toReal() + f.view()->property("cellHeight").toReal() * (900 / columns));
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        f.h->window->hide();
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); // the placement waiting behind the definitive order
        f.h->window->show();
        const auto anchored = [&] { const int top = f.rowAtViewTop(), row = f.h->fileGrid.rowOf(anchor); return top >= 0 && row / columns == top / columns; };
        QTRY_VERIFY_WITH_TIMEOUT(anchored(), 10000);
    }
    void keyboardDuringArrivalIsNotReplaced() {
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        f.view()->forceActiveFocus();
        for (int i = 0; i < 3; ++i) QTest::keyClick(f.h->window, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.cursorRow(), 2, 10000);
        const QString chosen = f.h->fileGrid.pathAt(2);
        QVERIFY(!f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{chosen});
    }
    void selectAllDuringArrivalIsNotReplaced() {
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        f.view()->forceActiveFocus();
        QTest::keyPress(f.h->window, Qt::Key_Control);
        QVERIFY(f.placing()); // nothing has happened yet
        QTest::keyRelease(f.h->window, Qt::Key_Control);
        QTest::keyClick(f.h->window, Qt::Key_A, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedCount(), kHeldAfter, 10000);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QCOMPARE(f.h->fileGrid.selectedCount(), kHeldAfter);
    }
    void menuSelectionDuringArrivalIsNotReplaced_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<int>("selected");
        QTest::newRow("select all") << "select-all" << kHeldAfter;
        QTest::newRow("grid menu select all") << "grid-select-all" << kHeldAfter;
        QTest::newRow("invert") << "invert-selection" << kHeldAfter;
        QTest::newRow("clear") << "clear-selection" << 0;
    }
    void menuSelectionDuringArrivalIsNotReplaced() {
        QFETCH(QString, command);
        QFETCH(int, selected);
        ArrivalFixture f; QVERIFY(f.start());
        QCOMPARE(f.h->fileGrid.selectedCount(), 0);
        QVERIFY(f.placing());
        QVERIFY(QMetaObject::invokeMethod(f.h->window, "invoke", Q_ARG(QVariant, command))); // what a menu item does
        QCOMPARE(f.h->fileGrid.selectedCount(), selected);
        QVERIFY(!f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QCOMPARE(f.h->fileGrid.selectedCount(), selected);
    }
    void otherCommandsDuringArrivalKeepThePlacement_data() {
        QTest::addColumn<QString>("command");
        for (const char* id : {"fit", "toggle-metadata", "grid-zoom-in", "filter"}) QTest::newRow(id) << QString(id);
    }
    void otherCommandsDuringArrivalKeepThePlacement() {
        QFETCH(QString, command);
        ArrivalFixture f; QVERIFY(f.start());
        QVERIFY(f.placing());
        f.h->viewer.trigger(command);
        QVERIFY(f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
    }
    // The grid's own buttons show for a selection kept across leaving and re-entering, with another photo viewed meanwhile.
    void gridButtonsDuringArrivalAreNotReplaced_data() {
        QTest::addColumn<QString>("button");
        QTest::newRow("clear") << "gridClearSelection";
        QTest::newRow("invert") << "gridInvertSelection";
    }
    void gridButtonsDuringArrivalAreNotReplaced() {
        QFETCH(QString, button);
        Media media{2, QSize(64, 48)};
        const auto names = [](int i) { return i == 2500 ? QString("p1.png") : i == 2600 ? QString("p2.png") : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0')); };
        FakeFolder plain; plain.total = kRows; plain.name = names;
        FakeFolder held; held.total = kRows; held.name = names; held.heldAfter = kHeldAfter;
        auto useHeld = std::make_shared<std::atomic_bool>(false);
        SeamScope seam(nullptr, 64);
        directory_scan::testSeam.enumerator = [a = plain.enumerator(), b = held.enumerator(), useHeld](
            const std::filesystem::path& d, bool hidden, std::stop_token cancel, std::size_t limit, const directory_scan::Sink& sink) {
            return (useHeld->load() ? b : a)(d, hidden, cancel, limit, sink);
        };
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.path(0));
        h.viewer.trigger("grid");
        auto* grid = h.window->findChild<QQuickItem*>("photoGrid");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == kRows && h.viewer.imageReady() && !grid->property("placeCursor").toBool(), 15000);
        runPlacements();
        // Rows that arrive first, so they are among the held rows.
        h.fileGrid.select(h.fileGrid.rowOf(media.dir.filePath("f03000.txt")));
        h.fileGrid.select(h.fileGrid.rowOf(media.dir.filePath("f02990.txt")), DirectoryEntryModel::Toggle);
        h.fileGrid.select(h.fileGrid.rowOf(media.dir.filePath("f02980.txt")), DirectoryEntryModel::Toggle);
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!grid->isVisible() && h.fileGrid.count() == 0, 10000);
        h.viewer.open(media.path(1)); // now the placement has somewhere else to go
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        useHeld->store(true);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(grid->isVisible() && h.fileGrid.count() == kHeldAfter && h.fileGrid.arriving(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.selectedCount(), 3, 10000);
        QVERIFY(grid->property("placeCursor").toBool());
        auto* chip = named(h.window, button);
        QVERIFY(chip);
        QTRY_VERIFY_WITH_TIMEOUT(chip->isVisible() && chip->height() > 0 && h.window->isExposed(), 10000);
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, sceneCenter(chip));
        const int afterClick = h.fileGrid.selectedCount();
        QVERIFY(afterClick != 3);
        QVERIFY(!grid->property("placeCursor").toBool());
        held.release();
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == kRows, 10000);
        runPlacements();
        QCOMPARE(h.fileGrid.selectedCount(), afterClick);
    }
    // What a screen reader's "press" does on a row. A non-photo stays in the grid, selected, so the placement must not replace it.
    void accessiblePressDuringArrivalIsNotReplaced() {
        ArrivalFixture f;
        f.customName = [](int i) { return i == 2500 ? QString("p1.png") : QString("f%1.dat").arg(kRows - i, 5, 10, QChar('0')); };
        QVERIFY(f.start());
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), 2) != nullptr, 10000);
        const QString path = f.h->fileGrid.pathAt(2);
        QCOMPARE(f.h->fileGrid.kindAt(2), QString("file"));
        QAccessible::setActive(true);
        QAccessibleInterface* cell = QAccessible::queryAccessibleInterface(cellForRow(f.view(), 2));
        QVERIFY(cell); QVERIFY(cell->actionInterface());
        cell->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{path}, 10000);
        QVERIFY(!f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{path});
    }
    // R2-B2: read at the definitive order itself, which comes before any frame. A frame sampler alone can pass with no frame drawn.
    void theViewIsOnTheAnchorBeforeAnyFrameAfterTheDefinitiveOrder_data() {
        QTest::addColumn<int>("folderEvery");
        QTest::newRow("files only") << 0;
        QTest::newRow("every 10th a folder") << 10;
        QTest::newRow("every 7th a folder") << 7;
    }
    void theViewIsOnTheAnchorBeforeAnyFrameAfterTheDefinitiveOrder() {
        QFETCH(int, folderEvery);
        ArrivalFixture f;
        if (folderEvery) f.customName = [folderEvery](int i) {
            return i == 2500 ? QString("p1.png") : i % folderEvery == 0 ? QString("dir%1").arg(kRows - i, 5, 10, QChar('0')) : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0'));
        };
        QVERIFY(f.start());
        QMetaObject::invokeMethod(f.view(), "movementStarted");
        f.scrollDown(600);
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        const int columns = f.columns();
        const qreal cell = f.view()->property("cellHeight").toReal();
        const auto line = [&] { return (f.contentY() - f.view()->property("originY").toReal()) / cell; };
        qreal linesOff = -1; int rowsMoved = 0;
        const int rowBefore = f.h->fileGrid.rowOf(anchor);
        connect(&f.h->fileGrid, &DirectoryEntryModel::arrivingChanged, this, [&] {
            if (f.h->fileGrid.arriving()) return;
            rowsMoved = std::abs(f.h->fileGrid.rowOf(anchor) - rowBefore);
            linesOff = std::abs(line() - f.h->fileGrid.rowOf(anchor) / columns);
        });
        QList<int> frameDrift;
        connect(f.h->window, &QQuickWindow::frameSwapped, this, [&] {
            const int row = f.h->fileGrid.rowOf(anchor), top = f.rowAtViewTop();
            if (!f.h->fileGrid.arriving() && row >= 0 && top >= 0) frameDrift << std::abs(row / columns - top / columns);
        }, Qt::DirectConnection);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QVERIFY(linesOff >= 0);
        QVERIFY(rowsMoved > 100 * columns); // the order did move rows, or this proves nothing
        QVERIFY2(linesOff < 1, qPrintable(QString("%1 lines from the anchor at the definitive order").arg(linesOff)));
        runPlacements();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(anchor), 10000);
        for (const int drift : std::as_const(frameDrift)) QCOMPARE(drift, 0);
    }
    void escapeFromTypeAheadReturnsToTheSameFile() {
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        f.view()->forceActiveFocus();
        QTRY_VERIFY_WITH_TIMEOUT(f.view()->hasActiveFocus(), 10000);
        for (int i = 0; i < 3; ++i) QTest::keyClick(f.h->window, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.cursorRow(), 2, 10000);
        const QString before = f.h->fileGrid.pathAt(2);
        QTest::keyClick(f.h->window, Qt::Key_2);
        QTRY_VERIFY_WITH_TIMEOUT(f.photoGrid()->property("searchQuery").toString() == "2", 10000);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QVERIFY(f.h->fileGrid.rowOf(before) != 2); // the order did move it
        if (f.photoGrid()->property("searchQuery").toString() != "2") QSKIP("the 3 s search timer ran out: this run proves nothing");
        QTest::keyClick(f.h->window, Qt::Key_Escape);
        QCOMPARE(f.photoGrid()->property("searchQuery").toString(), QString());
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{before});
    }
    void theFooterSaysSoFarWhileRowsArrive() {
        const auto footerShows = [](QQuickWindow* window, const QString& text) {
            const auto items = window->findChildren<QQuickItem*>();
            return std::any_of(items.cbegin(), items.cend(), [&](QQuickItem* item) { return item->isVisible() && item->property("text").toString() == text; });
        };
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        const QString arrived = QLocale().toString(kHeldAfter);
        f.view()->forceActiveFocus();
        QTRY_VERIFY_WITH_TIMEOUT(f.view()->hasActiveFocus(), 10000);
        QTest::keyClick(f.h->window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(f.h->fileGrid.cursorRow() >= 0, 10000);
        const int cursor = f.h->fileGrid.cursorRow() + 1;
        QTRY_VERIFY_WITH_TIMEOUT(footerShows(f.h->window, QString("%1 of %2 so far").arg(cursor).arg(arrived)), 10000);
        QTest::keyClick(f.h->window, Qt::Key_A, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(footerShows(f.h->window, QString("%1 selected of %2 so far").arg(arrived, arrived)), 10000);
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(footerShows(f.h->window, QString("%1 selected").arg(kHeldAfter)), 10000);
        f.h->fileGrid.select(4);
        QTRY_VERIFY_WITH_TIMEOUT(footerShows(f.h->window, QString("5 of %1").arg(kRows)), 10000);
    }
    // Opt-in measure of what one selectionChanged costs in a settled 100k grid, against the 6 ms drain budget.
    void traceSelectionChangedCost() {
        if (qEnvironmentVariableIsEmpty("GLANCELOT_PERF_ENUM_SELECTION")) QSKIP("set GLANCELOT_PERF_ENUM_SELECTION to measure, or to a row count");
        const int total = std::max(100000, qEnvironmentVariableIntValue("GLANCELOT_PERF_ENUM_SELECTION"));
        Media media{1, QSize(64, 48)};
        FakeFolder folder; folder.total = total;
        folder.name = [total](int i) { return i == total - 1 ? QString("p1.png") : QString("f%1.txt").arg(total - i, 7, 10, QChar('0')); };
        SeamScope seam(&folder);
        directory_scan::testSeam.tuning.entryStep = std::size_t(total); directory_scan::testSeam.tuning.entryLimit = std::size_t(total);
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.path(0));
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == total && h.viewer.imageReady(), 240000);
        runPlacements();
        const auto report = [](const char* what, std::vector<qint64> times) {
            std::sort(times.begin(), times.end());
            qInfo().noquote() << QString("PERF_ENUM_SELECTION %1 runs=%2 median_us=%3 worst_us=%4").arg(what).arg(times.size()).arg(times[times.size() / 2] / 1000).arg(times.back() / 1000);
        };
        const auto timed = [](int runs, const std::function<void(int)>& work) {
            std::vector<qint64> times;
            for (int i = 0; i < runs; ++i) { QElapsedTimer clock; clock.start(); work(i); times.push_back(clock.nsecsElapsed()); }
            return times;
        };
        const auto emission = [&](int) { QMetaObject::invokeMethod(&h.fileGrid, "selectionChanged"); };
        h.fileGrid.clearSelection();
        report("emit-no-selection", timed(41, emission));
        h.fileGrid.select(10);
        report("emit-one-selected", timed(41, emission));
        report("select-next-row", timed(41, [&](int i) { h.fileGrid.select(11 + i % 2); }));
        h.fileGrid.selectAll();
        report("emit-all-selected", timed(41, emission));
        // The commands themselves, with every binding that reads the selection.
        std::vector<qint64> all, clear, invert;
        for (int i = 0; i < 5; ++i) {
            QElapsedTimer clock;
            clock.start(); h.fileGrid.clearSelection(); clear.push_back(clock.nsecsElapsed());
            clock.restart(); h.fileGrid.selectAll(); all.push_back(clock.nsecsElapsed());
            clock.restart(); h.fileGrid.invertSelection(); invert.push_back(clock.nsecsElapsed());
            h.fileGrid.selectAll();
        }
        report(qPrintable(QString("select-all rows=%1").arg(total)), all);
        report(qPrintable(QString("clear-all rows=%1").arg(total)), clear);
        report(qPrintable(QString("invert-all rows=%1").arg(total)), invert);
    }
    void aKeyBeforeAnyRowKeepsThePlacement() {
        ArrivalFixture f; f.heldAfter = 0; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        f.view()->forceActiveFocus();
        QTest::keyClick(f.h->window, Qt::Key_Right);
        QVERIFY(f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{f.media.path(0)}, 10000);
    }
    void scrollbarDuringArrivalKeepsTheView() {
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        const QPoint track = f.view()->mapToScene(QPointF(f.view()->width() - 3, f.view()->height() * 0.6)).toPoint();
        const qreal before = f.contentY();
        QTest::mousePress(f.h->window, Qt::LeftButton, Qt::NoModifier, track);
        QTest::mouseRelease(f.h->window, Qt::LeftButton, Qt::NoModifier, track);
        QTRY_VERIFY_WITH_TIMEOUT(f.contentY() > before + 100, 10000);
        QVERIFY(!f.placing());
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(anchor), 10000);
        QVERIFY(f.h->fileGrid.selectedPaths().isEmpty());
    }
    void rightClickDuringArrivalIsNotReplaced() {
        ArrivalFixture f; QVERIFY(f.start()); QVERIFY(f.readyForInput());
        QTRY_VERIFY_WITH_TIMEOUT(cellForRow(f.view(), 2) != nullptr, 10000);
        const QString chosen = f.h->fileGrid.pathAt(2);
        QTest::mouseClick(f.h->window, Qt::RightButton, Qt::NoModifier, sceneCenter(cellForRow(f.view(), 2)));
        QTRY_COMPARE_WITH_TIMEOUT(f.h->fileGrid.selectedPaths(), QStringList{chosen}, 10000);
        QVERIFY(!f.placing());
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        QCOMPARE(f.h->fileGrid.selectedPaths(), QStringList{chosen});
    }
    // Folders landing with the definitive order, in one call: the position read has to be against the rows just inserted.
    void foldersInTheLastRowsKeepTheAnchor() {
        ArrivalFixture f;
        f.customName = [](int i) { return i == 2500 ? QString("p1.png") : i % 10 == 0 ? QString("dir%1").arg(kRows - i, 5, 10, QChar('0')) : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0')); };
        QVERIFY(f.start());
        QMetaObject::invokeMethod(f.view(), "movementStarted");
        f.scrollDown(600);
        QTRY_VERIFY_WITH_TIMEOUT(f.rowAtViewTop() > 0, 10000);
        const QString anchor = f.h->fileGrid.pathAt(f.rowAtViewTop());
        f.h->window->hide(); // no frames, so everything lands in what the view sees as one step
        f.folder.release();
        QTRY_VERIFY_WITH_TIMEOUT(f.settled(), 10000);
        runPlacements();
        f.h->window->show();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(anchor), 10000);
    }
    void tryAgainOnAnArrivingFolderReturnsToTheBrowsedPosition() {
        RetryFixture f; QVERIFY(f.start(true));
        QVERIFY(f.fail());
        f.source->store(RetryFixture::Lists);
        QSignalSpy arriving(&f.h->fileGrid, &DirectoryEntryModel::arrivingChanged);
        f.h->fileGrid.refresh();
        QVERIFY(f.recovered());
        QVERIFY(arriving.count() >= 2);
        runPlacements();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(f.anchor), 10000);
    }
    // No layout change fires here, and the rows may not have reached the view: the return has to manage on its own.
    void tryAgainWithTheOrderUnchangedReturnsToTheBrowsedPosition() {
        RetryFixture f; QVERIFY(f.start(false));
        QSignalSpy layouts(&f.h->fileGrid, &QAbstractItemModel::layoutChanged);
        QVERIFY(f.fail());
        f.source->store(RetryFixture::Lists);
        f.h->window->hide();
        f.h->fileGrid.refresh();
        QVERIFY(f.recovered());
        runPlacements();
        f.h->window->show();
        QCOMPARE(layouts.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(f.anchor), 10000);
    }
    // Partway, so rows were showing when it failed again: their position is not the one to return to.
    void failingAgainKeepsTheBrowsedPositionForTheNextTry() {
        RetryFixture f; QVERIFY(f.start(true));
        QVERIFY(f.fail());
        QVERIFY(f.fail(RetryFixture::FailsPartway));
        f.source->store(RetryFixture::Lists);
        f.h->fileGrid.refresh();
        QVERIFY(f.recovered());
        runPlacements();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(f.anchor), 10000);
    }
    void scrollingDuringTryAgainWinsOverTheBrowsedPosition() {
        RetryFixture f; QVERIFY(f.start(true));
        QVERIFY(f.fail());
        f.source->store(RetryFixture::ListsAndHolds);
        f.h->fileGrid.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(f.h->fileGrid.count() == kHeldAfter && f.view()->property("count").toInt() == kHeldAfter, 10000);
        QMetaObject::invokeMethod(f.view(), "movementStarted");
        f.view()->setProperty("contentY", f.view()->property("originY").toReal() + f.view()->property("cellHeight").toReal() * (300 / f.columns()));
        QTRY_VERIFY_WITH_TIMEOUT(f.top() > 0, 10000);
        const QString chosen = f.h->fileGrid.pathAt(f.top());
        QVERIFY(chosen != f.anchor);
        f.holding.release();
        QVERIFY(f.recovered());
        runPlacements();
        QTRY_VERIFY_WITH_TIMEOUT(f.anchoredOn(chosen), 10000);
    }
    void renamedWhileHiddenKeepsTheCursor() {
        Media media{6, QSize(64, 48)};
        ProbeWindow probes;
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.path(0));
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.viewer.trigger("grid");
        auto* grid = h.window->findChild<QQuickItem*>("photoGrid");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == 6 && grid->isVisible() && !grid->property("placeCursor").toBool(), 15000);
        h.fileGrid.select(h.fileGrid.rowOf(media.path(3)));
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!grid->isVisible() && h.fileGrid.count() == 0, 10000);
        const QString renamed = media.dir.filePath("renamed.png");
        QVERIFY(QFile::rename(media.path(3), renamed));
        probes.hold->store(true);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(grid->isVisible() && h.fileGrid.count() == 6 && !h.fileGrid.arriving() && h.fileGrid.loading(), 10000);
        runPlacements();
        QVERIFY(grid->property("placeCursor").toBool()); // still waiting: the baseline decides
        probes.released->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading(), 10000);
        runPlacements();
        QCOMPARE(h.fileGrid.selectedPaths(), QStringList{renamed});
    }
    void aClickInTheProbeWindowDecidesThePlacement() {
        Media media{6, QSize(64, 48)};
        ProbeWindow probes;
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.path(0));
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.viewer.trigger("grid");
        auto* grid = h.window->findChild<QQuickItem*>("photoGrid");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == 6 && grid->isVisible() && !grid->property("placeCursor").toBool(), 15000);
        h.fileGrid.select(h.fileGrid.rowOf(media.path(3)));
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!grid->isVisible() && h.fileGrid.count() == 0, 10000);
        QVERIFY(QFile::rename(media.path(3), media.dir.filePath("renamed.png")));
        probes.hold->store(true);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(grid->isVisible() && h.fileGrid.count() == 6 && !h.fileGrid.arriving() && h.fileGrid.loading(), 10000);
        runPlacements();
        const int row = h.fileGrid.rowOf(media.path(1));
        QVERIFY(QMetaObject::invokeMethod(grid, "clickRow", Q_ARG(QVariant, row), Q_ARG(QVariant, 0)));
        QVERIFY(!grid->property("placeCursor").toBool());
        probes.released->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading(), 10000);
        runPlacements();
        QCOMPARE(h.fileGrid.selectedPaths(), QStringList{media.path(1)});
    }
    // The cursor already has its row at the definitive order, so the view need not wait for the baseline to show it.
    void reenteringPlacesTheCursorAtTheDefinitiveOrder() {
        Media media{1, QSize(64, 48)};
        FakeFolder folder; folder.total = kRows;
        folder.name = [](int i) { return i == 2500 ? QString("p1.png") : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0')); };
        ProbeWindow probes;
        directory_scan::testSeam.tuning.patienceMs = 0; directory_scan::testSeam.tuning.minArrivalRows = 1;
        directory_scan::testSeam.tuning.batchRows = 64; directory_scan::testSeam.tuning.flushMs = 60000;
        directory_scan::testSeam.enumerator = folder.enumerator();
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.setFilmstripVisible(false);
        h.viewer.open(media.path(0));
        h.viewer.trigger("grid");
        auto* grid = h.window->findChild<QQuickItem*>("photoGrid");
        auto* view = h.window->findChild<QQuickItem*>("photoGridView");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading() && h.fileGrid.count() == kRows && h.viewer.imageReady() && !grid->property("placeCursor").toBool(), 15000);
        h.fileGrid.select(2000);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(!grid->isVisible() && h.fileGrid.count() == 0, 10000);
        probes.hold->store(true);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(grid->isVisible() && h.fileGrid.count() == kRows && !h.fileGrid.arriving() && h.fileGrid.loading(), 10000);
        const int cursor = h.fileGrid.cursorRow();
        QVERIFY(cursor > 1000);
        const auto top = [&] {
            int row = -1;
            QMetaObject::invokeMethod(view, "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, view->property("contentX").toReal() + 4), Q_ARG(qreal, view->property("contentY").toReal() + 4));
            return row;
        };
        QTRY_VERIFY_WITH_TIMEOUT(top() > 1000, 10000);
        QVERIFY(h.fileGrid.loading());
        probes.released->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileGrid.loading(), 10000);
    }
    void oneItemSoFarReadsInTheSingular() {
        ArrivalFixture f; f.heldAfter = 1; f.batchRows = 1; QVERIFY(f.start());
        QTRY_COMPARE_WITH_TIMEOUT(f.countText(), QString("1 item so far"), 10000);
    }
    void oneMatchSoFarReadsInTheSingular() {
        ArrivalFixture f; QVERIFY(f.start());
        f.h->fileGrid.setFilter("f02000", true); // i == 1000: already here
        QTRY_COMPARE_WITH_TIMEOUT(f.countText(), QString("1 match so far"), 10000);
    }
};
QTEST_MAIN(PerfEnumUiTests)
#include "perf_enum_ui_tests.moc"
