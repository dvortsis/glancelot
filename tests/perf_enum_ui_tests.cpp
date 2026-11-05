#include "ui_test_support.hpp"
#include "perf_enum_test_support.hpp"
#include <QAccessible>
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
    std::unique_ptr<SeamScope> seam;
    std::unique_ptr<Harness> h;
    bool start() {
        folder.total = kRows; folder.heldAfter = kHeldAfter;
        folder.name = [photo = photoAt](int i) { return i == photo ? QString("p1.png") : QString("f%1.txt").arg(kRows - i, 5, 10, QChar('0')); };
        seam = std::make_unique<SeamScope>(&folder);
        h = std::make_unique<Harness>();
        if (!h->window) return false;
        h->window->resize(1180, 780);
        h->viewer.setFilmstripVisible(false);
        h->viewer.open(media.path(0));
        h->viewer.trigger("grid");
        h->window->requestActivate();
        return QTest::qWaitFor([&] { return h->viewer.imageReady() && h->fileGrid.count() == kHeldAfter && h->fileGrid.loading() && view() && view()->property("count").toInt() == kHeldAfter; }, 15000);
    }
    // Continuation: listed in pages of 1000 up to 3000, each page landing above the rows already showing.
    bool startPaged(int total) {
        folder.total = total;
        folder.name = [photo = photoAt, total](int i) { return i == photo ? QString("p1.png") : QString("f%1.txt").arg(total - i, 5, 10, QChar('0')); };
        seam = std::make_unique<SeamScope>(&folder);
        directory_scan::testSeam.tuning.entryStep = kPage; directory_scan::testSeam.tuning.entryLimit = 3000;
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
    }
    void bannerFitsAtNarrowAndWideWidths() {
        ArrivalFixture f; f.photoAt = 300; QVERIFY(f.startPaged(2500));
        QQuickItem* text = named(f.h->window, "gridTruncatedText");
        const QByteArray shots = qgetenv("GLANCELOT_PERF_ENUM_SHOTS");
        for (const int width : {560, 1180}) {
            f.h->window->resize(width, 780);
            QTRY_COMPARE_WITH_TIMEOUT(f.h->window->width(), width, 10000);
            const auto fits = [&] {
                const qreal button = f.showMore()->x();
                // In the window, not just in the banner: a layout that has not caught up with the resize is cut off.
                return f.showMore()->isVisible() && f.showMore()->mapToScene({f.showMore()->width(), 0}).x() <= width
                    && f.banner()->width() > 0 && text->x() + text->width() <= button && button + f.showMore()->width() <= f.banner()->width()
                    && f.showMore()->y() >= 0 && f.showMore()->y() + f.showMore()->height() <= f.banner()->height()
                    && text->y() >= 0 && text->y() + text->height() <= f.banner()->height();
            };
            QTRY_VERIFY2_WITH_TIMEOUT(fits(), qPrintable(QString("banner does not fit at %1").arg(width)), 10000);
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
};
QTEST_MAIN(PerfEnumUiTests)
#include "perf_enum_ui_tests.moc"
