#include "glancelot/directory_entry_model.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void touch(const QString& path, int bytes = 1) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(bytes, 'x'));
}
bool settle(DirectoryEntryModel& model, int timeout = 10000) {
    QElapsedTimer t; t.start();
    while (model.loading() && t.elapsed() < timeout) QTest::qWait(5);
    return !model.loading();
}
}

class GridSearchTests : public QObject {
    Q_OBJECT
private slots:
    void emptyModelReturnsNoMatches() {
        DirectoryEntryModel model;
        QCOMPARE(model.findMatch("test"), -1);
        QCOMPARE(model.matchCount("test"), 0);
        QCOMPARE(model.matchIndex("test", 0), 0);
    }

    void substringAndCaseInsensitiveMatching() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("Summer_Vacation");
        touch(dir.filePath("vacation_photo1.jpg"));
        touch(dir.filePath("vacation_photo2.png"));
        touch(dir.filePath("winter_cabin.jpg"));
        touch(dir.filePath("notes.txt"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 5);

        // "vac" should match Summer_Vacation (folder), vacation_photo1.jpg, vacation_photo2.png = 3 matches
        QCOMPARE(model.matchCount("vac"), 3);
        QCOMPARE(model.matchCount("VACATION"), 3);
        QCOMPARE(model.matchCount("photo"), 2);
        QCOMPARE(model.matchCount("winter"), 1);
        QCOMPARE(model.matchCount("nonexistent"), 0);

        // Find match forward
        const int firstVac = model.findMatch("vac", 0, true);
        QVERIFY(firstVac >= 0);
        QCOMPARE(model.matchIndex("vac", firstVac), 1);

        // Find next match forward
        const int secondVac = model.findMatch("vac", (firstVac + 1) % model.count(), true);
        QVERIFY(secondVac >= 0 && secondVac != firstVac);
        QCOMPARE(model.matchIndex("vac", secondVac), 2);

        // Find match backward
        const int prevVac = model.findMatch("vac", (secondVac - 1 + model.count()) % model.count(), false);
        QCOMPARE(prevVac, firstVac);
    }

    void navigationCyclingForwardAndBackward() {
        QTemporaryDir dir;
        touch(dir.filePath("alfa_01.jpg"));
        touch(dir.filePath("bravo.jpg"));
        touch(dir.filePath("alfa_02.jpg"));
        touch(dir.filePath("charlie.jpg"));
        touch(dir.filePath("alfa_03.jpg"));
        touch(dir.filePath("delta.jpg"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 6);

        // Verify match count for multiple matches
        QCOMPARE(model.matchCount("alfa"), 3);

        const int row1 = model.rowOf(dir.filePath("alfa_01.jpg"));
        const int row2 = model.rowOf(dir.filePath("alfa_02.jpg"));
        const int row3 = model.rowOf(dir.filePath("alfa_03.jpg"));
        QVERIFY(row1 >= 0 && row2 >= 0 && row3 >= 0);

        // Step forward through all matches
        const int fwd1 = model.findMatch("alfa", 0, true);
        QCOMPARE(fwd1, row1);
        QCOMPARE(model.matchIndex("alfa", fwd1), 1);

        const int fwd2 = model.findMatch("alfa", (fwd1 + 1) % model.count(), true);
        QCOMPARE(fwd2, row2);
        QCOMPARE(model.matchIndex("alfa", fwd2), 2);

        const int fwd3 = model.findMatch("alfa", (fwd2 + 1) % model.count(), true);
        QCOMPARE(fwd3, row3);
        QCOMPARE(model.matchIndex("alfa", fwd3), 3);

        // Wraparound forward returns to the first match
        const int fwdWrap = model.findMatch("alfa", (fwd3 + 1) % model.count(), true);
        QCOMPARE(fwdWrap, row1);
        QCOMPARE(model.matchIndex("alfa", fwdWrap), 1);

        // Step backward through all matches
        // From row1, backward wraps around to row3
        const int bwd1 = model.findMatch("alfa", (row1 - 1 + model.count()) % model.count(), false);
        QCOMPARE(bwd1, row3);
        QCOMPARE(model.matchIndex("alfa", bwd1), 3);

        const int bwd2 = model.findMatch("alfa", (bwd1 - 1 + model.count()) % model.count(), false);
        QCOMPARE(bwd2, row2);
        QCOMPARE(model.matchIndex("alfa", bwd2), 2);

        const int bwd3 = model.findMatch("alfa", (bwd2 - 1 + model.count()) % model.count(), false);
        QCOMPARE(bwd3, row1);
        QCOMPARE(model.matchIndex("alfa", bwd3), 1);

        // Single match cycling: wraps to itself forward and backward
        QCOMPARE(model.matchCount("bravo"), 1);
        const int rowBravo = model.rowOf(dir.filePath("bravo.jpg"));
        QVERIFY(rowBravo >= 0);

        const int singleFwd = model.findMatch("bravo", (rowBravo + 1) % model.count(), true);
        QCOMPARE(singleFwd, rowBravo);
        const int singleBwd = model.findMatch("bravo", (rowBravo - 1 + model.count()) % model.count(), false);
        QCOMPARE(singleBwd, rowBravo);
    }

    void prefixAndSubstringMatching() {
        QTemporaryDir dir;
        touch(dir.filePath("photo_camera.jpg"));
        touch(dir.filePath("awesome_photo.jpg"));
        touch(dir.filePath("my_camera_roll.jpg"));
        touch(dir.filePath("scenery.png"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 4);

        // "photo" matches photo_camera (prefix) and awesome_photo (substring)
        QCOMPARE(model.matchCount("photo"), 2);
        const int rowPhoto1 = model.findMatch("photo", 0, true);
        QVERIFY(rowPhoto1 >= 0);
        const int rowPhoto2 = model.findMatch("photo", (rowPhoto1 + 1) % model.count(), true);
        QVERIFY(rowPhoto2 >= 0 && rowPhoto2 != rowPhoto1);

        // Substring matches within middle of filename
        QCOMPARE(model.matchCount("era"), 2); // photo_camera, my_camera_roll
        QCOMPARE(model.matchCount("roll"), 1); // my_camera_roll
        QCOMPARE(model.matchCount("scene"), 1); // scenery
        QCOMPARE(model.matchCount("nery"), 1); // scenery
        QCOMPARE(model.matchCount("some"), 1); // awesome_photo
    }

    void caseInsensitivity() {
        QTemporaryDir dir;
        touch(dir.filePath("MixedCase_Alpha.JPG"));
        touch(dir.filePath("UPPERCASE_BETA.PNG"));
        touch(dir.filePath("lowercase_gamma.jpg"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 3);

        // Search with various casing
        QCOMPARE(model.matchCount("mixedcase"), 1);
        QCOMPARE(model.matchCount("MIXEDCASE"), 1);
        QCOMPARE(model.matchCount("MiXeDcAsE"), 1);
        QCOMPARE(model.findMatch("MIXEDCASE"), model.findMatch("mixedcase"));

        QCOMPARE(model.matchCount("beta"), 1);
        QCOMPARE(model.matchCount("BETA"), 1);
        QCOMPARE(model.matchCount("gamma"), 1);
        QCOMPARE(model.matchCount("GAMMA"), 1);

        // Extension case insensitivity
        QCOMPARE(model.matchCount(".jpg"), 2);
        QCOMPARE(model.matchCount(".JPG"), 2);
        QCOMPARE(model.matchCount(".png"), 1);
        QCOMPARE(model.matchCount(".PNG"), 1);
    }

    void emptyAndNonExistentQueryHandling() {
        QTemporaryDir dir;
        touch(dir.filePath("item1.png"));
        touch(dir.filePath("item2.png"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 2);

        // Empty query
        QCOMPARE(model.findMatch(""), -1);
        QCOMPARE(model.findMatch("   "), -1);
        QCOMPARE(model.matchCount(""), 0);
        QCOMPARE(model.matchCount("   "), 0);
        QCOMPARE(model.matchIndex("", 0), 0);
        QCOMPARE(model.matchIndex("   ", 0), 0);

        // Non-existent query
        QCOMPARE(model.findMatch("zebra_nonexistent"), -1);
        QCOMPARE(model.matchCount("zebra_nonexistent"), 0);
        QCOMPARE(model.matchIndex("zebra_nonexistent", 0), 0);

        // Out-of-bounds start row clamping
        QVERIFY(model.findMatch("item", -5, true) >= 0);
        QVERIFY(model.findMatch("item", 999, true) >= 0);
        QCOMPARE(model.matchIndex("item", -1), 0);
        QCOMPARE(model.matchIndex("item", 999), 0);
    }

    void behaviorWhenDirectoryIsRelisted() {
        QTemporaryDir dir;
        touch(dir.filePath("apple_01.jpg"));
        touch(dir.filePath("apple_02.jpg"));
        touch(dir.filePath("orange.jpg"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.matchCount("apple"), 2);
        QCOMPARE(model.matchCount("banana"), 0);
        QCOMPARE(model.matchCount("orange"), 1);

        // Modify files on disk: add new files and remove existing ones
        touch(dir.filePath("banana_01.jpg"));
        touch(dir.filePath("banana_02.jpg"));
        touch(dir.filePath("apple_03.jpg"));
        QVERIFY(QFile::remove(dir.filePath("apple_01.jpg")));
        QVERIFY(QFile::remove(dir.filePath("orange.jpg")));

        model.refresh();
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 4); // apple_02, apple_03, banana_01, banana_02

        // Verify updated match counts and finds
        QCOMPARE(model.matchCount("apple"), 2);
        QCOMPARE(model.matchCount("banana"), 2);
        QCOMPARE(model.matchCount("orange"), 0);
        QCOMPARE(model.findMatch("orange"), -1);

        const int bananaMatch = model.findMatch("banana", 0, true);
        QVERIFY(bananaMatch >= 0);
        QCOMPARE(model.matchIndex("banana", bananaMatch), 1);

        // apple_01 was deleted, so "01" should only match banana_01
        QCOMPARE(model.matchCount("01"), 1);
        const int row01 = model.findMatch("01", 0, true);
        QCOMPARE(row01, model.rowOf(dir.filePath("banana_01.jpg")));

        // apple_03 was added, so "03" should match apple_03
        QCOMPARE(model.matchCount("03"), 1);
        const int row03 = model.findMatch("03", 0, true);
        QCOMPARE(row03, model.rowOf(dir.filePath("apple_03.jpg")));
    }

    void invertSelectionTogglesAllRows() {
        QTemporaryDir dir;
        touch(dir.filePath("a.png"));
        touch(dir.filePath("b.png"));
        touch(dir.filePath("c.png"));

        DirectoryEntryModel model;
        model.setActive(true);
        model.setDirectory(dir.path());
        QVERIFY(settle(model));
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.selectedCount(), 0);

        // Select first item
        model.select(0, DirectoryEntryModel::Replace);
        QCOMPARE(model.selectedCount(), 1);

        // Invert: now 2 items selected
        model.invertSelection();
        QCOMPARE(model.selectedCount(), 2);
        const QStringList paths = model.selectedPaths();
        QVERIFY(!paths.contains(model.pathAt(0)));
        QVERIFY(paths.contains(model.pathAt(1)));
        QVERIFY(paths.contains(model.pathAt(2)));

        // Invert again: back to first item
        model.invertSelection();
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(model.selectedPaths().first(), model.pathAt(0));
    }
};

QTEST_MAIN(GridSearchTests)
#include "grid_search_tests.moc"
