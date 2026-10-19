#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QtTest>

class UiSettingsTests : public QObject {
    Q_OBJECT
private slots:
    void initialSettingsStub() {
        ViewerController viewer;
        QCOMPARE(viewer.textScale(), 1.0);
        QCOMPARE(viewer.reducedMotion(), false);
    }
};

QTEST_MAIN(UiSettingsTests)
#include "ui_settings_tests.moc"
