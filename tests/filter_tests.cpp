#include "glancelot/directory_entry_model.hpp"
#include "glancelot/filter_state.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void touch(const QString& path, int bytes = 1) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x'));
}

QStringList names(const DirectoryEntryModel& model) {
    QStringList out;
    for (int i = 0; i < model.count(); ++i) {
        out << model.data(model.index(i), DirectoryEntryModel::NameRole).toString();
    }
    return out;
}

bool settle(DirectoryEntryModel& model, int timeout = 10000) {
    QElapsedTimer t;
    t.start();
    while (model.loading() && t.elapsed() < timeout) {
        QTest::qWait(5);
    }
    return !model.loading();
}
}

class FilterTests : public QObject {
    Q_OBJECT

private slots:
    void filterSpecBasics();
    void directoryModelFilterMatches();
    void directoryModelSelectedCountWithFilter();
    void directoryModelTemporaryFilterClearsOnFolderChange();
    void directoryModelPersistentFilterSurvivesFolderChange();
    void viewerControllerFilterCommands();
    void viewerIntraFolderNavigationObeysFilter();
    void viewerCrossingSuccessClearsTemporaryFilter();
    void viewerCrossingSuccessRetainsPersistentFilter();
    void viewerCrossingFailurePreservesTemporaryFilter();
    void viewerTraversalCrossingClearsTemporaryFilter();
};

void FilterTests::filterSpecBasics() {
    glancelot::FilterSpec spec;
    QVERIFY(!spec.isActive());
    QVERIFY(!spec.isPersistent());
    QVERIFY(spec.matches("anything.png"));
    QVERIFY(spec.matches(""));

    spec.pattern = "sun";
    QVERIFY(spec.isActive());
    QVERIFY(spec.matches("sunset.jpg"));
    QVERIFY(spec.matches("SUNSET.PNG"));
    QVERIFY(!spec.matches("beach.png"));

    spec.caseSensitive = true;
    QVERIFY(spec.matches("sunset.jpg"));
    QVERIFY(!spec.matches("SUNSET.PNG"));

    spec.persistence = glancelot::FilterPersistence::Persistent;
    QVERIFY(spec.isPersistent());

    spec.clear();
    QVERIFY(!spec.isActive());
    QVERIFY(spec.matches("beach.png"));
}

void FilterTests::directoryModelFilterMatches() {
    QTemporaryDir dir;
    touch(dir.filePath("apple.jpg"));
    touch(dir.filePath("apricot.png"));
    touch(dir.filePath("banana.jpg"));
    touch(dir.filePath("berry.png"));

    DirectoryEntryModel model;
    model.setActive(true);
    model.setDirectory(dir.path());
    QVERIFY(settle(model));
    QCOMPARE(model.count(), 4);

    QSignalSpy filterSpy(&model, &DirectoryEntryModel::filterChanged);
    model.setFilter("ap");
    QVERIFY(model.hasFilter());
    QCOMPARE(model.filterPattern(), QStringLiteral("ap"));
    QCOMPARE(model.count(), 2);
    QStringList currentNames = names(model);
    QVERIFY(currentNames.contains("apple.jpg"));
    QVERIFY(currentNames.contains("apricot.png"));
    QVERIFY(!currentNames.contains("banana.jpg"));
    QCOMPARE(filterSpy.count(), 1);

    model.clearFilter();
    QVERIFY(!model.hasFilter());
    QCOMPARE(model.count(), 4);
}

void FilterTests::directoryModelSelectedCountWithFilter() {
    QTemporaryDir dir;
    touch(dir.filePath("cat1.jpg"));
    touch(dir.filePath("cat2.jpg"));
    touch(dir.filePath("dog1.jpg"));
    touch(dir.filePath("dog2.jpg"));

    DirectoryEntryModel model;
    model.setActive(true);
    model.setDirectory(dir.path());
    QVERIFY(settle(model));
    QCOMPARE(model.count(), 4);

    // Select cat1, dog1
    model.select(model.rowOf(dir.filePath("cat1.jpg")), DirectoryEntryModel::Toggle);
    model.select(model.rowOf(dir.filePath("dog1.jpg")), DirectoryEntryModel::Toggle);
    QCOMPARE(model.selectedCount(), 2);

    // Apply filter "cat" -> only cat1 and cat2 visible
    model.setFilter("cat");
    QCOMPARE(model.count(), 2);
    // selectedCount must only count visible matching selected items
    QCOMPARE(model.selectedCount(), 1);

    // Clear filter -> 2 selected again
    model.clearFilter();
    QCOMPARE(model.selectedCount(), 2);
}

void FilterTests::directoryModelTemporaryFilterClearsOnFolderChange() {
    QTemporaryDir dirA;
    touch(dirA.filePath("photo1.jpg"));
    touch(dirA.filePath("photo2.jpg"));

    QTemporaryDir dirB;
    touch(dirB.filePath("photo3.jpg"));

    DirectoryEntryModel model;
    model.setActive(true);
    model.setDirectory(dirA.path());
    QVERIFY(settle(model));

    model.setFilter("photo1", false);
    QVERIFY(model.hasFilter());
    QVERIFY(!model.isFilterPersistent());
    QCOMPARE(model.count(), 1);

    // Switch to folder B: temporary filter must clear!
    model.setDirectory(dirB.path());
    QVERIFY(settle(model));
    QVERIFY(!model.hasFilter());
    QCOMPARE(model.filterPattern(), QString());
    QCOMPARE(model.count(), 1);
}

void FilterTests::directoryModelPersistentFilterSurvivesFolderChange() {
    QTemporaryDir dirA;
    touch(dirA.filePath("target_a.jpg"));
    touch(dirA.filePath("other_a.jpg"));

    QTemporaryDir dirB;
    touch(dirB.filePath("target_b.jpg"));
    touch(dirB.filePath("other_b.jpg"));

    DirectoryEntryModel model;
    model.setActive(true);
    model.setDirectory(dirA.path());
    QVERIFY(settle(model));

    model.setFilter("target", true);
    QVERIFY(model.hasFilter());
    QVERIFY(model.isFilterPersistent());
    QCOMPARE(model.count(), 1);
    QCOMPARE(names(model), QStringList{"target_a.jpg"});

    // Switch to folder B: persistent filter must survive!
    model.setDirectory(dirB.path());
    QVERIFY(settle(model));
    QVERIFY(model.hasFilter());
    QVERIFY(model.isFilterPersistent());
    QCOMPARE(model.filterPattern(), QStringLiteral("target"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(names(model), QStringList{"target_b.jpg"});
}

void FilterTests::viewerControllerFilterCommands() {
    ViewerController viewer;
    QSignalSpy filterSpy(&viewer, &ViewerController::filterChanged);

    viewer.setFilter("vacation", false);
    QVERIFY(viewer.hasFilter());
    QCOMPARE(viewer.filterPattern(), QStringLiteral("vacation"));
    QVERIFY(!viewer.isFilterPersistent());
    QCOMPARE(filterSpy.count(), 1);

    viewer.trigger("toggle-filter-persistence");
    QVERIFY(viewer.isFilterPersistent());
    QCOMPARE(filterSpy.count(), 2);

    viewer.trigger("clear-filter");
    QVERIFY(!viewer.hasFilter());
    QVERIFY(viewer.filterPattern().isEmpty());
    QCOMPARE(filterSpy.count(), 3);
}

void FilterTests::viewerIntraFolderNavigationObeysFilter() {
    QTemporaryDir dir;
    touch(dir.filePath("1_red.jpg"));
    touch(dir.filePath("2_blue.jpg"));
    touch(dir.filePath("3_red.jpg"));
    touch(dir.filePath("4_blue.jpg"));

    ViewerController viewer;
    viewer.setBoundaryMode("loop");
    viewer.open(dir.filePath("1_red.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("1_red.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Set filter for "red"
    viewer.setFilter("red", false);
    QVERIFY(viewer.hasFilter());

    // Navigating next should skip 2_blue.jpg and land directly on 3_red.jpg!
    viewer.next();
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("3_red.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Navigating next again in loop mode should loop back to 1_red.jpg!
    viewer.next();
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("1_red.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Navigating previous should step back to 3_red.jpg
    viewer.previous();
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("3_red.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Filter remains active throughout intra-folder traversal
    QVERIFY(viewer.hasFilter());
}

void FilterTests::viewerCrossingSuccessClearsTemporaryFilter() {
    QTemporaryDir rootDir;
    QDir dir(rootDir.path());
    dir.mkdir("folderA");
    dir.mkdir("folderB");
    touch(dir.filePath("folderA/img1.jpg"));
    touch(dir.filePath("folderB/pic1.jpg"));

    ViewerController viewer;
    viewer.open(dir.filePath("folderA/img1.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("img1.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Set temporary filter
    viewer.setFilter("img", false);
    QVERIFY(viewer.hasFilter());
    QVERIFY(!viewer.isFilterPersistent());

    // Cross into next folder via open()
    viewer.open(dir.filePath("folderB/pic1.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("pic1.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Successful crossing into folderB must clear temporary filter!
    QVERIFY(!viewer.hasFilter());
    QVERIFY(viewer.filterPattern().isEmpty());
}

void FilterTests::viewerCrossingSuccessRetainsPersistentFilter() {
    QTemporaryDir rootDir;
    QDir dir(rootDir.path());
    dir.mkdir("folderA");
    dir.mkdir("folderB");
    touch(dir.filePath("folderA/img1.jpg"));
    touch(dir.filePath("folderB/img2.jpg"));

    ViewerController viewer;
    viewer.open(dir.filePath("folderA/img1.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("img1.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Set persistent filter
    viewer.setFilter("img", true);
    QVERIFY(viewer.hasFilter());
    QVERIFY(viewer.isFilterPersistent());

    // Cross into next folder via open()
    viewer.open(dir.filePath("folderB/img2.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("img2.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Persistent filter must survive the crossing!
    QVERIFY(viewer.hasFilter());
    QVERIFY(viewer.isFilterPersistent());
    QCOMPARE(viewer.filterPattern(), QStringLiteral("img"));
}

void FilterTests::viewerCrossingFailurePreservesTemporaryFilter() {
    QTemporaryDir rootDir;
    QDir dir(rootDir.path());
    touch(dir.filePath("a.jpg"));

    ViewerController viewer;
    viewer.setBoundaryMode("stop");
    viewer.open(dir.filePath("a.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("a.jpg"));
    QTRY_VERIFY(!viewer.loading());

    viewer.setFilter("a", false);
    QVERIFY(viewer.hasFilter());
    QVERIFY(!viewer.isFilterPersistent());

    // In Stop mode, stepping past 'a' fails to cross (boundary reached)
    viewer.next();
    QTest::qWait(100);

    // Crossing did not succeed: original image and temporary filter must be preserved!
    QCOMPARE(viewer.fileName(), QStringLiteral("a.jpg"));
    QVERIFY(viewer.hasFilter());
    QCOMPARE(viewer.filterPattern(), QStringLiteral("a"));
}

void FilterTests::viewerTraversalCrossingClearsTemporaryFilter() {
    QTemporaryDir rootDir;
    QDir dir(rootDir.path());
    touch(dir.filePath("start.jpg"));
    dir.mkdir("sub");
    touch(dir.filePath("sub/next.jpg"));

    ViewerController viewer;
    viewer.setBoundaryMode("continue");
    viewer.open(dir.filePath("start.jpg"));
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("start.jpg"));
    QTRY_VERIFY(!viewer.loading());

    viewer.setFilter("start", false);
    QVERIFY(viewer.hasFilter());

    // Step next crosses into subfolder
    viewer.next();
    QTRY_COMPARE(viewer.fileName(), QStringLiteral("next.jpg"));
    QTRY_VERIFY(!viewer.loading());

    // Successful crossing into subfolder clears temporary filter!
    QVERIFY(!viewer.hasFilter());
    QVERIFY(viewer.filterPattern().isEmpty());
}

QTEST_MAIN(FilterTests)
#include "filter_tests.moc"
