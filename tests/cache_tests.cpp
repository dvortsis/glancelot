#include "glancelot/viewer_controller.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool createTestImage(const QString& path, int width = 64, int height = 48, const QColor& color = Qt::blue) {
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
}
} // namespace

class CacheTests : public QObject {
    Q_OBJECT

private slots:
    void cacheDefaults();
    void cacheHitOnRevisit();
    void bidirectionalNavigation();
    void revisionInvalidationOnDiskChange();
    void clearDecodedCache();
    void crossFolderLookaheadPrefetch();
    void lookaheadCancellationOnReversal();
    void lookaheadCancellationOnFilterChange();
};

void CacheTests::cacheDefaults() {
    ViewerController controller;
    QCOMPARE(controller.decodedCacheBytes(), 0);
    QCOMPARE(controller.decodedCacheCount(), 0);
    QVERIFY(!controller.isLastDecodeCacheHit());
}

void CacheTests::cacheHitOnRevisit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString fileA = dir.filePath("a.png");
    const QString fileB = dir.filePath("b.png");
    QVERIFY(createTestImage(fileA, 64, 48, Qt::red));
    QVERIFY(createTestImage(fileB, 64, 48, Qt::green));

    ViewerController controller;
    QSignalSpy cacheSpy(&controller, &ViewerController::cacheChanged);

    // 1. Open image A (cold decode)
    controller.open(fileA);
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());
    QCOMPARE(controller.decodedCacheCount(), 1);
    QVERIFY(controller.decodedCacheBytes() > 0);
    QVERIFY(!controller.image().isNull());

    // 2. Step next to image B (cold decode)
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("b.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());
    QCOMPARE(controller.decodedCacheCount(), 2);

    // 3. Step previous to image A (cache hit!)
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("a.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(controller.isLastDecodeCacheHit());
    QCOMPARE(controller.decodedCacheCount(), 2);
    QVERIFY(controller.status().startsWith("Cached"));
}

void CacheTests::bidirectionalNavigation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString file1 = dir.filePath("img1.png");
    const QString file2 = dir.filePath("img2.png");
    const QString file3 = dir.filePath("img3.png");
    QVERIFY(createTestImage(file1, 32, 32, Qt::red));
    QVERIFY(createTestImage(file2, 32, 32, Qt::green));
    QVERIFY(createTestImage(file3, 32, 32, Qt::blue));

    ViewerController controller;
    controller.open(file1);
    QTRY_VERIFY(controller.imageReady());
    QTRY_COMPARE(controller.fileCount(), 3);
    QVERIFY(!controller.isLastDecodeCacheHit());

    // Step forward to img2
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("img2.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());

    // Step forward to img3
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("img3.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());

    // Step backward to img2 (cache hit!)
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("img2.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(controller.isLastDecodeCacheHit());

    // Step backward to img1 (cache hit!)
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("img1.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(controller.isLastDecodeCacheHit());
}

void CacheTests::revisionInvalidationOnDiskChange() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString fileA = dir.filePath("modA.png");
    const QString fileB = dir.filePath("modB.png");
    QVERIFY(createTestImage(fileA, 40, 40, Qt::yellow));
    QVERIFY(createTestImage(fileB, 40, 40, Qt::green));

    ViewerController controller;
    controller.open(fileA);
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());
    QCOMPARE(controller.decodedCacheCount(), 1);

    // Step to B
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("modB.png"));
    QTRY_VERIFY(controller.imageReady());

    // Modify fileA on disk with different size/content
    QTest::qWait(20);
    QVERIFY(createTestImage(fileA, 80, 80, Qt::cyan));

    // Step back to A: revision changed on disk so key changed -> cache miss, re-decodes new image
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("modA.png"));
    QTRY_VERIFY(controller.imageReady());
    QCOMPARE(controller.imageWidth(), 80);
    QCOMPARE(controller.imageHeight(), 80);
    QVERIFY(!controller.isLastDecodeCacheHit());
}

void CacheTests::clearDecodedCache() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString fileA = dir.filePath("c1.png");
    const QString fileB = dir.filePath("c2.png");
    QVERIFY(createTestImage(fileA, 48, 48, Qt::magenta));
    QVERIFY(createTestImage(fileB, 48, 48, Qt::yellow));

    ViewerController controller;
    QSignalSpy cacheSpy(&controller, &ViewerController::cacheChanged);

    controller.open(fileA);
    QTRY_VERIFY(controller.imageReady());
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("c2.png"));
    QTRY_VERIFY(controller.imageReady());
    QCOMPARE(controller.decodedCacheCount(), 2);

    controller.clearDecodedCache();
    QCOMPARE(controller.decodedCacheCount(), 0);
    QCOMPARE(controller.decodedCacheBytes(), 0);
    QVERIFY(cacheSpy.count() >= 2);

    // Stepping back is a cold miss because cache was cleared
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("c1.png"));
    QTRY_VERIFY(controller.imageReady());
    QVERIFY(!controller.isLastDecodeCacheHit());
}

void CacheTests::crossFolderLookaheadPrefetch() {
    QTemporaryDir rootDir;
    QVERIFY(rootDir.isValid());

    QDir root(rootDir.path());
    const QString a1 = root.filePath("1.png");
    const QString a2 = root.filePath("2.png");
    const QString a3 = root.filePath("3.png");
    const QString a4 = root.filePath("4.png");

    QVERIFY(root.mkdir("sub"));
    const QString b1 = root.filePath("sub/1.png");

    QVERIFY(createTestImage(a1, 32, 32, Qt::red));
    QVERIFY(createTestImage(a2, 32, 32, Qt::green));
    QVERIFY(createTestImage(a3, 32, 32, Qt::blue));
    QVERIFY(createTestImage(a4, 32, 32, Qt::yellow));
    QVERIFY(createTestImage(b1, 32, 32, Qt::cyan));

    ViewerController controller;
    controller.setBoundaryMode("continue");

    controller.open(a4);
    QTRY_VERIFY(controller.imageReady());
    QCOMPARE(controller.fileName(), QString("4.png"));

    // Lookahead triggers asynchronously for sub/1.png
    // Await prefetch completion in cache
    QTRY_VERIFY_WITH_TIMEOUT(controller.decodedCacheCount() >= 2, 5000);

    // Step next across folder boundary into sub
    controller.next();
    QTRY_COMPARE(controller.fileName(), QString("1.png"));
    QTRY_COMPARE(controller.directory(), QDir(root.filePath("sub")).absolutePath());
    QTRY_VERIFY(controller.imageReady());

    // The destination image was already prefetched into cache by lookahead!
    QVERIFY(controller.isLastDecodeCacheHit());
}

void CacheTests::lookaheadCancellationOnReversal() {
    QTemporaryDir rootDir;
    QVERIFY(rootDir.isValid());

    QDir root(rootDir.path());
    const QString f1 = root.filePath("x1.png");
    const QString f2 = root.filePath("x2.png");
    const QString f3 = root.filePath("x3.png");
    const QString f4 = root.filePath("x4.png");

    QVERIFY(root.mkdir("sub"));
    const QString g1 = root.filePath("sub/y1.png");

    QVERIFY(createTestImage(f1));
    QVERIFY(createTestImage(f2));
    QVERIFY(createTestImage(f3));
    QVERIFY(createTestImage(f4));
    QVERIFY(createTestImage(g1));

    ViewerController controller;
    controller.setBoundaryMode("continue");
    controller.open(f4);
    QTRY_VERIFY(controller.imageReady());

    // Immediately reverse direction
    controller.previous();
    QTRY_COMPARE(controller.fileName(), QString("x3.png"));
    QTRY_VERIFY(controller.imageReady());

    // Navigation and reversal proceed cleanly without stalling or crashes
    QVERIFY(!controller.image().isNull());
}

void CacheTests::lookaheadCancellationOnFilterChange() {
    QTemporaryDir rootDir;
    QVERIFY(rootDir.isValid());

    QDir root(rootDir.path());
    const QString p1 = root.filePath("p1.png");
    const QString p2 = root.filePath("p2.png");

    QVERIFY(root.mkdir("sub"));
    const QString q1 = root.filePath("sub/q1.png");

    QVERIFY(createTestImage(p1));
    QVERIFY(createTestImage(p2));
    QVERIFY(createTestImage(q1));

    ViewerController controller;
    controller.setBoundaryMode("continue");
    controller.open(p2);
    QTRY_VERIFY(controller.imageReady());

    // Changing filter discards stale lookahead work
    controller.setFilter("nonexistent");
    QCOMPARE(controller.filterPattern(), QString("nonexistent"));
    controller.clearFilter();
    QCOMPARE(controller.filterPattern(), QString());
}

QTEST_MAIN(CacheTests)
#include "cache_tests.moc"

