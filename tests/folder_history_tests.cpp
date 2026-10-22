#include "glancelot/folder_history.hpp"
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>

class FolderHistoryTests : public QObject {
    Q_OBJECT

private slots:
    void initialState();
    void distinctFolderVisits();
    void intraFolderStepsDoNotDuplicate();
    void goBackAndForward();
    void branchingNavigationClearsForward();
    void capacityBounds();
    void duplicateVisitSuppression();
    void missingDirectoryRecovery();
    void windowsCaseInsensitivity();
    void clearHistory();
};

void FolderHistoryTests::initialState() {
    FolderHistory history;
    QCOMPARE(history.canGoBack(), false);
    QCOMPARE(history.canGoForward(), false);
    QCOMPARE(history.backCount(), 0);
    QCOMPARE(history.forwardCount(), 0);
    QVERIFY(history.backDirectory().isEmpty());
    QVERIFY(history.forwardDirectory().isEmpty());
    QVERIFY(history.currentDirectory().isEmpty());
    QVERIFY(!history.currentEntry().has_value());
}

void FolderHistoryTests::distinctFolderVisits() {
    FolderHistory history;
    QSignalSpy backSpy(&history, &FolderHistory::canGoBackChanged);
    QSignalSpy forwardSpy(&history, &FolderHistory::canGoForwardChanged);

    history.recordVisit("/photos/vacation", "/photos/vacation/p1.jpg");
    QCOMPARE(history.canGoBack(), false);
    QCOMPARE(history.canGoForward(), false);
    QCOMPARE(history.currentDirectory(), QDir::cleanPath("/photos/vacation"));

    history.recordVisit("/photos/work", "/photos/work/w1.png");
    QCOMPARE(history.canGoBack(), true);
    QCOMPARE(history.canGoForward(), false);
    QCOMPARE(history.backCount(), 1);
    QCOMPARE(history.backDirectory(), QDir::cleanPath("/photos/vacation"));
    QCOMPARE(history.currentDirectory(), QDir::cleanPath("/photos/work"));
    QCOMPARE(backSpy.count(), 1);

    history.recordVisit("/photos/family", "/photos/family/f1.jpg");
    QCOMPARE(history.canGoBack(), true);
    QCOMPARE(history.backCount(), 2);
    QCOMPARE(history.backDirectory(), QDir::cleanPath("/photos/work"));
    QCOMPARE(history.currentDirectory(), QDir::cleanPath("/photos/family"));
}

void FolderHistoryTests::intraFolderStepsDoNotDuplicate() {
    FolderHistory history;
    history.recordVisit("/photos/album", "/photos/album/img1.jpg");
    QCOMPARE(history.backCount(), 0);

    // Intra-folder step: step to next photo in the same directory
    history.recordVisit("/photos/album", "/photos/album/img2.jpg");
    QCOMPARE(history.backCount(), 0);
    QCOMPARE(history.canGoBack(), false);

    auto entry = history.currentEntry();
    QVERIFY(entry.has_value());
    QCOMPARE(entry->filePath, QString("/photos/album/img2.jpg"));

    // Update via updateCurrent
    history.updateCurrent("/photos/album/img3.jpg", "/photos/album/img3.jpg", 5);
    entry = history.currentEntry();
    QVERIFY(entry.has_value());
    QCOMPARE(entry->filePath, QString("/photos/album/img3.jpg"));
    QCOMPARE(entry->selectedPath, QString("/photos/album/img3.jpg"));
    QCOMPARE(entry->scrollPosition, 5);
    QCOMPARE(history.backCount(), 0);
}

void FolderHistoryTests::goBackAndForward() {
    FolderHistory history;
    history.recordVisit("/photos/a", "/photos/a/1.jpg", "/photos/a/1.jpg", 10);
    history.recordVisit("/photos/b", "/photos/b/2.jpg", "/photos/b/2.jpg", 20);
    history.recordVisit("/photos/c", "/photos/c/3.jpg", "/photos/c/3.jpg", 30);

    QCOMPARE(history.backCount(), 2);
    QCOMPARE(history.forwardCount(), 0);

    // Go back from c to b
    auto b = history.goBack();
    QVERIFY(b.has_value());
    QCOMPARE(b->directory, QDir::cleanPath("/photos/b"));
    QCOMPARE(b->filePath, QString("/photos/b/2.jpg"));
    QCOMPARE(b->scrollPosition, 20);
    QCOMPARE(history.canGoBack(), true);
    QCOMPARE(history.canGoForward(), true);
    QCOMPARE(history.forwardDirectory(), QDir::cleanPath("/photos/c"));

    // Go back from b to a
    auto a = history.goBack();
    QVERIFY(a.has_value());
    QCOMPARE(a->directory, QDir::cleanPath("/photos/a"));
    QCOMPARE(a->filePath, QString("/photos/a/1.jpg"));
    QCOMPARE(history.canGoBack(), false);
    QCOMPARE(history.canGoForward(), true);

    // Attempting goBack when empty returns nullopt
    auto none = history.goBack();
    QVERIFY(!none.has_value());

    // Go forward from a to b
    auto fwdB = history.goForward();
    QVERIFY(fwdB.has_value());
    QCOMPARE(fwdB->directory, QDir::cleanPath("/photos/b"));
    QCOMPARE(history.canGoBack(), true);
    QCOMPARE(history.canGoForward(), true);

    // Go forward from b to c
    auto fwdC = history.goForward();
    QVERIFY(fwdC.has_value());
    QCOMPARE(fwdC->directory, QDir::cleanPath("/photos/c"));
    QCOMPARE(history.canGoBack(), true);
    QCOMPARE(history.canGoForward(), false);

    // Attempting goForward when empty returns nullopt
    auto fwdNone = history.goForward();
    QVERIFY(!fwdNone.has_value());
}

void FolderHistoryTests::branchingNavigationClearsForward() {
    FolderHistory history;
    history.recordVisit("/photos/a");
    history.recordVisit("/photos/b");
    history.recordVisit("/photos/c");

    // Go back to b, then to a
    history.goBack();
    history.goBack();
    QCOMPARE(history.forwardCount(), 2);

    // Now visit a new branch d
    history.recordVisit("/photos/d");
    // Forward stack must be cleared
    QCOMPARE(history.forwardCount(), 0);
    QCOMPARE(history.canGoForward(), false);
    // Back stack should contain a
    QCOMPARE(history.backCount(), 1);
    QCOMPARE(history.backDirectory(), QDir::cleanPath("/photos/a"));
}

void FolderHistoryTests::capacityBounds() {
    FolderHistory history;
    history.setCapacity(3);
    QCOMPARE(history.capacity(), 3);

    for (int i = 1; i <= 6; ++i) {
        history.recordVisit(QString("/photos/folder%1").arg(i));
    }

    // Back stack should be capped at 3
    QCOMPARE(history.backCount(), 3);
    // Top of back stack is folder5, then folder4, then folder3
    QCOMPARE(history.backDirectory(), QDir::cleanPath("/photos/folder5"));

    auto prev = history.goBack();
    QVERIFY(prev.has_value());
    QCOMPARE(prev->directory, QDir::cleanPath("/photos/folder5"));

    prev = history.goBack();
    QVERIFY(prev.has_value());
    QCOMPARE(prev->directory, QDir::cleanPath("/photos/folder4"));

    prev = history.goBack();
    QVERIFY(prev.has_value());
    QCOMPARE(prev->directory, QDir::cleanPath("/photos/folder3"));

    // folder1 and folder2 were dropped due to capacity
    QCOMPARE(history.canGoBack(), false);
}

void FolderHistoryTests::duplicateVisitSuppression() {
    FolderHistory history;
    history.recordVisit("/photos/nature");
    QCOMPARE(history.backCount(), 0);

    // Visiting the exact same folder again is suppressed
    history.recordVisit("/photos/nature");
    history.recordVisit("/photos/nature/");
    QCOMPARE(history.backCount(), 0);
}

void FolderHistoryTests::missingDirectoryRecovery() {
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString dir1 = tempRoot.filePath("dir1");
    const QString dir2 = tempRoot.filePath("dir2");
    const QString dir3 = tempRoot.filePath("dir3");
    QDir().mkdir(dir1);
    QDir().mkdir(dir2);
    QDir().mkdir(dir3);

    FolderHistory history;
    history.recordVisit(dir1);
    history.recordVisit(dir2);
    history.recordVisit(dir3);
    QCOMPARE(history.backCount(), 2);

    // Delete dir2
    QDir(dir2).removeRecursively();
    QVERIFY(!QDir(dir2).exists());

    // When navigating back with navigateBack, stale dir2 is skipped and we arrive at dir1!
    QSignalSpy navSpy(&history, &FolderHistory::navigationRequested);
    bool ok = history.navigateBack();
    QVERIFY(ok);
    QCOMPARE(navSpy.count(), 1);
    QCOMPARE(navSpy.first().at(0).toString(), QDir::cleanPath(dir1));
    QCOMPARE(history.currentDirectory(), QDir::cleanPath(dir1));
}

void FolderHistoryTests::windowsCaseInsensitivity() {
#ifdef Q_OS_WIN
    FolderHistory history;
    history.recordVisit("C:/Photos/Album");
    history.recordVisit("c:/photos/album");
    // Should be treated as the same folder on Windows
    QCOMPARE(history.backCount(), 0);
#endif
}

void FolderHistoryTests::clearHistory() {
    FolderHistory history;
    history.recordVisit("/photos/x");
    history.recordVisit("/photos/y");
    QCOMPARE(history.canGoBack(), true);

    history.clear();
    QCOMPARE(history.canGoBack(), false);
    QCOMPARE(history.canGoForward(), false);
    QCOMPARE(history.backCount(), 0);
    QCOMPARE(history.forwardCount(), 0);
    QVERIFY(history.currentDirectory().isEmpty());
}

QTEST_MAIN(FolderHistoryTests)
#include "folder_history_tests.moc"

