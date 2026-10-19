#include "glancelot/directory_entry_model.hpp"
#include <QtTest>

class GridDensityTests : public QObject {
    Q_OBJECT
private slots:
    void initialBucketsAndSelection() {
        DirectoryEntryModel model;
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(model.selectedBytes(), 0);
        QCOMPARE(model.selectedSummary(), QString());
    }
};

QTEST_MAIN(GridDensityTests)
#include "grid_density_tests.moc"
