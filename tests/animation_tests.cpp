#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSignalSpy>
#include "glancelot/animation_controller.hpp"

class AnimationTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testInitialState();
    void testStillImageNonAnimation();
    void testAnimatedGifPlayback();
    void testSpeedControlAndClamping();
    void testLoopToggle();
    void testMultipageTiffNavigation();
    void testGenerationValidation();
    void testSeekAndClamp();
    void testClearLifecycle();

private:
    QTemporaryDir m_tempDir;
    QString m_gifPath;
    QString m_tiffPath;
    QString m_pngPath;
};

void AnimationTests::initTestCase() {
    QVERIFY(m_tempDir.isValid());

    // 1. Minimal 2-frame 2x2 animated GIF89a with 100ms and 200ms delays
    const unsigned char gifData[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, // GIF89a
        0x02, 0x00, 0x02, 0x00,             // width=2, height=2
        0x80, 0x00, 0x00,                   // GCT flag, 2 entries
        0xFF, 0x00, 0x00,                   // Color 0: Red
        0x00, 0x00, 0xFF,                   // Color 1: Blue
        // Netscape application block (loop forever)
        0x21, 0xFF, 0x0B,
        0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30,
        0x03, 0x01, 0x00, 0x00, 0x00,
        // Frame 1 GCE (delay 100ms = 10 hundredths)
        0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00,
        // Frame 1 Image Descriptor
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
        // Frame 1 LZW data
        0x02, 0x02, 0x44, 0x01, 0x00,
        // Frame 2 GCE (delay 200ms = 20 hundredths)
        0x21, 0xF9, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00,
        // Frame 2 Image Descriptor
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
        // Frame 2 LZW data
        0x02, 0x02, 0x4C, 0x01, 0x00,
        // Trailer
        0x3B
    };

    m_gifPath = m_tempDir.filePath("animated.gif");
    QFile gifFile(m_gifPath);
    QVERIFY(gifFile.open(QIODevice::WriteOnly));
    gifFile.write(reinterpret_cast<const char*>(gifData), sizeof(gifData));
    gifFile.close();

    // 2. Minimal 2-page TIFF document
    const unsigned char tiffData[] = {
        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, // II, 42, IFD0 offset 8
        // IFD0 (8 entries)
        0x08, 0x00,
        0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 256: Width = 2
        0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 257: Length = 2
        0x02, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, // 258: BitsPerSample = 8
        0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // 259: Compression = 1
        0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // 262: Photometric = 1
        0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD4, 0x00, 0x00, 0x00, // 273: StripOffsets = 212
        0x16, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 278: RowsPerStrip = 2
        0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, // 279: StripByteCounts = 4
        0x6E, 0x00, 0x00, 0x00, // Next IFD offset = 110
        // IFD1 (8 entries)
        0x08, 0x00,
        0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 256: Width = 2
        0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 257: Length = 2
        0x02, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, // 258: BitsPerSample = 8
        0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // 259: Compression = 1
        0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // 262: Photometric = 1
        0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD8, 0x00, 0x00, 0x00, // 273: StripOffsets = 216
        0x16, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // 278: RowsPerStrip = 2
        0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, // 279: StripByteCounts = 4
        0x00, 0x00, 0x00, 0x00, // Next IFD = 0 (End)
        // Pixel data: page 1 (4 bytes), page 2 (4 bytes)
        0x00, 0x55, 0xAA, 0xFF,
        0xFF, 0xAA, 0x55, 0x00
    };

    m_tiffPath = m_tempDir.filePath("multipage.tiff");
    QFile tiffFile(m_tiffPath);
    QVERIFY(tiffFile.open(QIODevice::WriteOnly));
    tiffFile.write(reinterpret_cast<const char*>(tiffData), sizeof(tiffData));
    tiffFile.close();

    // 3. Single-frame PNG
    m_pngPath = m_tempDir.filePath("single.png");
    QImage singleImg(4, 4, QImage::Format_RGBA8888);
    singleImg.fill(Qt::green);
    QVERIFY(singleImg.save(m_pngPath, "PNG"));
}

void AnimationTests::cleanupTestCase() {
    // TempDir auto-cleans on destruction
}

void AnimationTests::testInitialState() {
    glancelot::AnimationController controller;
    QCOMPARE(controller.isActive(), false);
    QCOMPARE(controller.isAnimated(), false);
    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.frameCount(), 0);
    QCOMPARE(controller.speed(), 1.0);
    QCOMPARE(controller.isLoop(), true);
    QCOMPARE(controller.currentDelayMs(), 100);
    QVERIFY(controller.frameLabel().isEmpty());
    QVERIFY(controller.frameImage().isNull());
}

void AnimationTests::testStillImageNonAnimation() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);
    controller.load(m_pngPath, 1);

    // Wait for worker to finish probing
    QTRY_COMPARE(loadedSpy.count(), 1);
    QCOMPARE(controller.isActive(), false);
    QCOMPARE(controller.isAnimated(), false);
    QCOMPARE(controller.frameCount(), 0);
    QCOMPARE(controller.isPlaying(), false);
    QVERIFY(controller.frameImage().isNull());
}

void AnimationTests::testAnimatedGifPlayback() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);

    controller.load(m_gifPath, 2);

    QTRY_COMPARE(loadedSpy.count(), 1);
    QCOMPARE(controller.isActive(), true);
    QCOMPARE(controller.isAnimated(), true);
    QCOMPARE(controller.frameCount(), 2);
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.currentDelayMs(), 100);
    QCOMPARE(controller.frameLabel(), QString("Frame 1 / 2"));
    QVERIFY(!controller.frameImage().isNull());

    // Auto-plays on open
    QTRY_VERIFY(controller.isPlaying());

    // Pause and Play
    controller.pause();
    QCOMPARE(controller.isPlaying(), false);

    controller.play();
    QCOMPARE(controller.isPlaying(), true);

    controller.togglePlayPause();
    QCOMPARE(controller.isPlaying(), false);

    // Explicitly reset to frame 0 to test deterministic stepping
    controller.jumpToFrame(0);
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.currentDelayMs(), 100);

    // Step next frame: from 0 to 1
    controller.nextFrame();
    QCOMPARE(controller.currentFrame(), 1);
    QCOMPARE(controller.currentDelayMs(), 200);
    QCOMPARE(controller.frameLabel(), QString("Frame 2 / 2"));

    // Step next frame again: wraps to 0 (since loop is true)
    controller.nextFrame();
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.currentDelayMs(), 100);
    QCOMPARE(controller.frameLabel(), QString("Frame 1 / 2"));

    // Step previous frame: wraps to 1
    controller.previousFrame();
    QCOMPARE(controller.currentFrame(), 1);
    QCOMPARE(controller.frameLabel(), QString("Frame 2 / 2"));
}

void AnimationTests::testSpeedControlAndClamping() {
    glancelot::AnimationController controller;
    controller.setSpeed(2.0);
    QCOMPARE(controller.speed(), 2.0);

    // Clamping limits
    controller.setSpeed(10.0);
    QCOMPARE(controller.speed(), 4.0);

    controller.setSpeed(0.01);
    QCOMPARE(controller.speed(), 0.1);

    controller.setSpeed(1.5);
    QCOMPARE(controller.speed(), 1.5);
}

void AnimationTests::testLoopToggle() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);
    controller.load(m_gifPath, 3);
    QTRY_COMPARE(loadedSpy.count(), 1);
    QVERIFY(controller.isActive());

    controller.pause();
    controller.setLoop(false);
    QCOMPARE(controller.isLoop(), false);

    controller.jumpToFrame(1);
    QCOMPARE(controller.currentFrame(), 1);

    // Stepping past end when loop is false stops at end
    controller.nextFrame();
    QCOMPARE(controller.currentFrame(), 1);

    // Stepping before start when loop is false stops at start
    controller.jumpToFrame(0);
    controller.previousFrame();
    QCOMPARE(controller.currentFrame(), 0);
}

void AnimationTests::testMultipageTiffNavigation() {
    if (!QImageReader::supportedImageFormats().contains("tiff") && !QImageReader::supportedImageFormats().contains("tif")) {
        QSKIP("TIFF format not supported by current Qt image formats plugins on this platform");
    }
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);
    controller.load(m_tiffPath, 4);

    QTRY_COMPARE(loadedSpy.count(), 1);
    QVERIFY(controller.isActive());
    // Requirement ADV-06: TIFF pages have NO invented timing and are not animated
    QCOMPARE(controller.isAnimated(), false);
    QCOMPARE(controller.frameCount(), 2);
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.frameLabel(), QString("Page 1 / 2"));

    // Multipage TIFF documents must NOT auto-play
    QCOMPARE(controller.isPlaying(), false);

    // Explicit play() should do nothing on document format
    controller.play();
    QCOMPARE(controller.isPlaying(), false);

    // Page navigation works
    controller.nextFrame();
    QCOMPARE(controller.currentFrame(), 1);
    QCOMPARE(controller.frameLabel(), QString("Page 2 / 2"));

    controller.previousFrame();
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.frameLabel(), QString("Page 1 / 2"));
}

void AnimationTests::testGenerationValidation() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);

    // Load with gen 100, then immediately gen 101 with another file
    controller.load(m_gifPath, 100);
    controller.load(m_pngPath, 101);

    // The result should converge to gen 101 (still image -> inactive)
    QTRY_VERIFY_WITH_TIMEOUT(loadedSpy.count() >= 1, 5000);
    QTRY_COMPARE(controller.isActive(), false);
    QCOMPARE(controller.frameCount(), 0);
}

void AnimationTests::testSeekAndClamp() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);
    controller.load(m_gifPath, 5);
    QTRY_COMPARE(loadedSpy.count(), 1);
    QVERIFY(controller.isActive());

    controller.pause();
    controller.jumpToFrame(999);
    QCOMPARE(controller.currentFrame(), 1); // Clamped to frameCount - 1

    controller.jumpToFrame(-50);
    QCOMPARE(controller.currentFrame(), 0); // Clamped to 0
}

void AnimationTests::testClearLifecycle() {
    glancelot::AnimationController controller;
    QSignalSpy loadedSpy(&controller, &glancelot::AnimationController::loaded);
    controller.load(m_gifPath, 6);
    QTRY_COMPARE(loadedSpy.count(), 1);
    QVERIFY(controller.isActive());

    controller.clear();
    QCOMPARE(controller.isActive(), false);
    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.frameCount(), 0);
    QCOMPARE(controller.currentFrame(), 0);
    QVERIFY(controller.frameImage().isNull());
    QVERIFY(controller.frameLabel().isEmpty());
}

QTEST_MAIN(AnimationTests)
#include "animation_tests.moc"

