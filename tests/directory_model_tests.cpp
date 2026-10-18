#include "glancelot/directory_entry_model.hpp"
#include <QtTest>

// Contract placeholder; FILE-GRID-01 adds async listing, generation, selection and bounds tests here.
class DirectoryModelTests : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault() {
        DirectoryEntryModel model;
        QCOMPARE(model.count(), 0); QCOMPARE(model.selectedCount(), 0); QCOMPARE(model.cursorRow(), -1);
        QVERIFY(model.thumbnailSource(0, 128).isEmpty());
    }
};
QTEST_GUILESS_MAIN(DirectoryModelTests)
#include "directory_model_tests.moc"
