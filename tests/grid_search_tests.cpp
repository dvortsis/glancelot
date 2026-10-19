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
