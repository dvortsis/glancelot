#include "glancelot/directory_entry_model.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void touch(const QString& path, qint64 bytes = 1) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    if (bytes > 0) {
        f.resize(bytes);
    }
    f.close();
}

bool settle(DirectoryEntryModel& model, int timeout = 10000) {
    QElapsedTimer t; t.start();
    while (model.loading() && t.elapsed() < timeout) QTest::qWait(5);
    return !model.loading();
}
}

class GridDensityTests : public QObject {
    Q_OBJECT
private slots:
    void initialBucketsAndSelection() {
        DirectoryEntryModel model;
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);
        QCOMPARE(model.selectedSummary(), QString());
        QVERIFY(model.selectedPaths().isEmpty());
    }

    void selectAllAndClearSelection() {
        QTemporaryDir dir;
        touch(dir.filePath("img1.jpg"), 100);
        touch(dir.filePath("img2.jpg"), 200);
        touch(dir.filePath("img3.png"), 300);
        touch(dir.filePath("doc.txt"), 400);

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.selectedCount(), 0);

        // Select All
        model.selectAll();
        QCOMPARE(model.selectedCount(), 4);
        QCOMPARE(model.selectedPaths().size(), 4);
        QCOMPARE(model.cursorRow(), 0);

        // Clear Selection
        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QVERIFY(model.selectedPaths().isEmpty());
        // Cursor remains at row 0 after clearSelection
        QCOMPARE(model.cursorRow(), 0);
    }

    void invertSelectionAcrossStates() {
        QTemporaryDir dir;
        touch(dir.filePath("a.jpg"), 10);
        touch(dir.filePath("b.jpg"), 20);
        touch(dir.filePath("c.jpg"), 30);
        touch(dir.filePath("d.jpg"), 40);
        touch(dir.filePath("e.jpg"), 50);

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 5);

        // 1. Invert on empty selection -> all 5 selected
        QCOMPARE(model.selectedCount(), 0);
        model.invertSelection();
        QCOMPARE(model.selectedCount(), 5);

        // 2. Invert when all selected -> 0 selected
        model.invertSelection();
        QCOMPARE(model.selectedCount(), 0);

        // 3. Partial selection: select rows 0 and 2
        model.select(0, DirectoryEntryModel::Replace);
        model.select(2, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 2);
        const QString path0 = model.pathAt(0);
        const QString path2 = model.pathAt(2);
        const QString path1 = model.pathAt(1);
        const QString path3 = model.pathAt(3);
        const QString path4 = model.pathAt(4);

        model.invertSelection();
        QCOMPARE(model.selectedCount(), 3);
        const QStringList sel = model.selectedPaths();
        QVERIFY(!sel.contains(path0));
        QVERIFY(!sel.contains(path2));
        QVERIFY(sel.contains(path1));
        QVERIFY(sel.contains(path3));
        QVERIFY(sel.contains(path4));
    }

    void selectedBytesAndFormatting() {
        QTemporaryDir dir;
        touch(dir.filePath("small.jpg"), 1024);            // 1 KB
        touch(dir.filePath("medium.jpg"), 2048);           // 2 KB
        touch(dir.filePath("large.png"), 1048576);         // 1 MB

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 3);

        const int rowSmall = model.rowOf(dir.filePath("small.jpg"));
        const int rowMedium = model.rowOf(dir.filePath("medium.jpg"));
        const int rowLarge = model.rowOf(dir.filePath("large.png"));
        QVERIFY(rowSmall >= 0 && rowMedium >= 0 && rowLarge >= 0);

        // Single selection
        model.select(rowSmall, DirectoryEntryModel::Replace);
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(model.selectedBytes(), 1024);
        const QString formattedSingle = model.selectedBytesFormatted();
        QVERIFY(formattedSingle.contains("1") && (formattedSingle.contains("KB") || formattedSingle.contains("KiB") || formattedSingle.contains("kB")));
        const QString summarySingle = model.selectedSummary();
        QVERIFY(summarySingle.startsWith("Selected: 1 item ("));
        QVERIFY(summarySingle.endsWith(")"));

        // Two items
        model.select(rowMedium, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(model.selectedBytes(), 1024 + 2048);
        const QString summaryTwo = model.selectedSummary();
        QVERIFY(summaryTwo.startsWith("Selected: 2 items ("));

        // All items
        model.selectAll();
        QCOMPARE(model.selectedCount(), 3);
        QCOMPARE(model.selectedBytes(), 1024 + 2048 + 1048576);
        const QString summaryAll = model.selectedSummary();
        QVERIFY(summaryAll.startsWith("Selected: 3 items ("));

        // Clear selection
        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);
        QVERIFY(model.selectedSummary().isEmpty());
    }

    void emptyDirectoryHandling() {
        QTemporaryDir dir;
        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);
        QVERIFY(model.selectedSummary().isEmpty());

        // Operations on empty directory must be safe no-ops
        model.selectAll();
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);

        model.invertSelection();
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);

        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);
    }

    void selectionWithFoldersAndZeroBytes() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("subfolder");
        touch(dir.filePath("empty.txt"), 0);
        touch(dir.filePath("regular.jpg"), 500);

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 3);

        const int rowFolder = model.rowOf(dir.filePath("subfolder"));
        const int rowEmpty = model.rowOf(dir.filePath("empty.txt"));
        const int rowRegular = model.rowOf(dir.filePath("regular.jpg"));
        QVERIFY(rowFolder >= 0 && rowEmpty >= 0 && rowRegular >= 0);

        // Select only folder
        model.select(rowFolder, DirectoryEntryModel::Replace);
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(model.selectedBytes(), 0);
        // When bytes is 0, summary formats without byte size
        QCOMPARE(model.selectedSummary(), QStringLiteral("Selected: 1 item"));

        // Select empty file too
        model.select(rowEmpty, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(model.selectedBytes(), 0);
        QCOMPARE(model.selectedSummary(), QStringLiteral("Selected: 2 items"));

        // Select regular file too
        model.select(rowRegular, DirectoryEntryModel::Toggle);
        QCOMPARE(model.selectedCount(), 3);
        QCOMPARE(model.selectedBytes(), 500);
        QVERIFY(model.selectedSummary().startsWith("Selected: 3 items ("));
    }

    void sizeBucketClampingAndStepping() {
        // Preset size buckets defined in PhotoGrid.qml
        const QVector<int> sizeBuckets = {96, 128, 160, 220, 320};
        const int defaultIndex = 2; // 160px default

        int bucketIndex = defaultIndex;
        QCOMPARE(sizeBuckets[bucketIndex], 160);

        auto zoomIn = [&]() {
            if (bucketIndex < sizeBuckets.size() - 1) ++bucketIndex;
        };
        auto zoomOut = [&]() {
            if (bucketIndex > 0) --bucketIndex;
        };
        auto resetZoom = [&]() {
            bucketIndex = defaultIndex;
        };
        auto thumbEdge = [&](int bucket) {
            return bucket >= 220 ? 512 : 256;
        };

        // Zoom In step-by-step
        zoomIn();
        QCOMPARE(bucketIndex, 3);
        QCOMPARE(sizeBuckets[bucketIndex], 220);
        QCOMPARE(thumbEdge(sizeBuckets[bucketIndex]), 512);

        zoomIn();
        QCOMPARE(bucketIndex, 4);
        QCOMPARE(sizeBuckets[bucketIndex], 320);
        QCOMPARE(thumbEdge(sizeBuckets[bucketIndex]), 512);

        // Clamping at upper boundary
        zoomIn();
        QCOMPARE(bucketIndex, 4);
        QCOMPARE(sizeBuckets[bucketIndex], 320);

        // Reset
        resetZoom();
        QCOMPARE(bucketIndex, 2);
        QCOMPARE(sizeBuckets[bucketIndex], 160);
        QCOMPARE(thumbEdge(sizeBuckets[bucketIndex]), 256);

        // Zoom Out step-by-step
        zoomOut();
        QCOMPARE(bucketIndex, 1);
        QCOMPARE(sizeBuckets[bucketIndex], 128);
        QCOMPARE(thumbEdge(sizeBuckets[bucketIndex]), 256);

        zoomOut();
        QCOMPARE(bucketIndex, 0);
        QCOMPARE(sizeBuckets[bucketIndex], 96);
        QCOMPARE(thumbEdge(sizeBuckets[bucketIndex]), 256);

        // Clamping at lower boundary
        zoomOut();
        QCOMPARE(bucketIndex, 0);
        QCOMPARE(sizeBuckets[bucketIndex], 96);

        // Reset from lowest boundary
        resetZoom();
        QCOMPARE(bucketIndex, 2);
        QCOMPARE(sizeBuckets[bucketIndex], 160);
    }
};

QTEST_MAIN(GridDensityTests)
#include "grid_density_tests.moc"
