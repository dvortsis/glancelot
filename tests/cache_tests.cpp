#include "glancelot/viewer_controller.hpp"
#include <QtTest>

class CacheTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cacheDefaults();
};

void CacheTests::initTestCase() {
}

void CacheTests::cacheDefaults() {
    ViewerController controller;
    QCOMPARE(controller.decodedCacheBytes(), 0);
    QCOMPARE(controller.decodedCacheCount(), 0);
    QVERIFY(!controller.isLastDecodeCacheHit());
}

QTEST_MAIN(CacheTests)
#include "cache_tests.moc"
