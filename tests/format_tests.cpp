#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include "glancelot/navigation.hpp"
#include "glancelot/raw_preview.hpp"

class FormatTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testCommonRasterExtensions();
    void testRawExtensions();
    void testUnsupportedExtensions();
    void testBmpDecode();
    void testRawPreviewExtractionFromTiff();
    void testRawPreviewFallbackScan();
    void testRawAbsentPreview();
    void testMalformedRawInputs();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void FormatTests::initTestCase() {
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
}

void FormatTests::testCommonRasterExtensions() {
    // FMT-02: bmp, tga, tif, tiff, gif
    const QStringList rasters = {
        QStringLiteral("photo.bmp"), QStringLiteral("photo.BMP"),
        QStringLiteral("photo.tga"), QStringLiteral("photo.TGA"),
        QStringLiteral("photo.tif"), QStringLiteral("photo.TIF"),
        QStringLiteral("photo.tiff"), QStringLiteral("photo.TIFF"),
        QStringLiteral("photo.jpg"), QStringLiteral("photo.jpeg"),
        QStringLiteral("photo.png"), QStringLiteral("photo.PNG")
    };

    for (const auto& name : rasters) {
        const std::filesystem::path p = name.toStdString();
        QVERIFY2(glancelot::isSupportedImage(p), qPrintable(name));
        QCOMPARE(glancelot::imageCategory(p), glancelot::ImageFormatCategory::Raster);
    }

    // FMT-02/FMT-03: gif, webp, apng (Animated)
    const QStringList animated = {
        QStringLiteral("anim.gif"), QStringLiteral("anim.GIF"),
        QStringLiteral("anim.webp"), QStringLiteral("anim.WEBP"),
        QStringLiteral("anim.apng"), QStringLiteral("anim.APNG")
    };

    for (const auto& name : animated) {
        const std::filesystem::path p = name.toStdString();
        QVERIFY2(glancelot::isSupportedImage(p), qPrintable(name));
        QCOMPARE(glancelot::imageCategory(p), glancelot::ImageFormatCategory::Animated);
    }
}

void FormatTests::testRawExtensions() {
    // FMT-06: dng, cr2, cr3, nef, nrw, arw, sr2, raf, orf, rw2, pef, iiq
    const QStringList raws = {
        QStringLiteral("shot.dng"), QStringLiteral("shot.DNG"),
        QStringLiteral("shot.cr2"), QStringLiteral("shot.CR2"),
        QStringLiteral("shot.cr3"), QStringLiteral("shot.CR3"),
        QStringLiteral("shot.nef"), QStringLiteral("shot.NEF"),
        QStringLiteral("shot.nrw"), QStringLiteral("shot.NRW"),
        QStringLiteral("shot.arw"), QStringLiteral("shot.ARW"),
        QStringLiteral("shot.sr2"), QStringLiteral("shot.SR2"),
        QStringLiteral("shot.raf"), QStringLiteral("shot.RAF"),
        QStringLiteral("shot.orf"), QStringLiteral("shot.ORF"),
        QStringLiteral("shot.rw2"), QStringLiteral("shot.RW2"),
        QStringLiteral("shot.pef"), QStringLiteral("shot.PEF"),
        QStringLiteral("shot.iiq"), QStringLiteral("shot.IIQ")
    };

    for (const auto& name : raws) {
        const std::filesystem::path p = name.toStdString();
        QVERIFY2(glancelot::isSupportedImage(p), qPrintable(name));
        QCOMPARE(glancelot::imageCategory(p), glancelot::ImageFormatCategory::Raw);
        QVERIFY2(glancelot::RawPreviewExtractor::isRawExtension(name), qPrintable(name));
    }
}

void FormatTests::testUnsupportedExtensions() {
    const QStringList unsupp = {
        QStringLiteral("doc.txt"), QStringLiteral("manual.pdf"),
        QStringLiteral("video.mp4"), QStringLiteral("archive.zip"),
        QStringLiteral("app.exe"), QStringLiteral("data.bin"),
        QStringLiteral("photo.png.txt"), QStringLiteral("noext")
    };

    for (const auto& name : unsupp) {
        const std::filesystem::path p = name.toStdString();
        QVERIFY2(!glancelot::isSupportedImage(p), qPrintable(name));
        QCOMPARE(glancelot::imageCategory(p), glancelot::ImageFormatCategory::Unsupported);
        QVERIFY2(!glancelot::RawPreviewExtractor::isRawExtension(name), qPrintable(name));
    }
}

void FormatTests::testBmpDecode() {
    const QString bmpPath = QDir(m_tempDir->path()).filePath(QStringLiteral("test.bmp"));
    QImage srcImg(120, 80, QImage::Format_RGB32);
    srcImg.fill(Qt::darkCyan);
    QVERIFY(srcImg.save(bmpPath, "BMP"));

    QImageReader reader(bmpPath);
    QVERIFY(reader.canRead());
    const QImage decoded = reader.read();
    QVERIFY(!decoded.isNull());
    QCOMPARE(decoded.width(), 120);
    QCOMPARE(decoded.height(), 80);
}

void FormatTests::testRawPreviewExtractionFromTiff() {
    // Create a mock TIFF-based DNG file with embedded JPEG preview
    // 1. Create a 320x240 test JPEG
    QByteArray jpegBytes;
    {
        QImage previewImg(320, 240, QImage::Format_RGB32);
        previewImg.fill(Qt::darkMagenta);
        QBuffer buf(&jpegBytes);
        buf.open(QIODevice::WriteOnly);
        previewImg.save(&buf, "JPEG");
    }
    QVERIFY(!jpegBytes.isEmpty());

    // 2. Build synthetic TIFF container
    // Header (8 bytes):
    // 0..1: 'I', 'I' (little endian)
    // 2..3: 42 (0x002A)
    // 4..7: Offset to IFD0 = 8
    // IFD0 (starts at 8):
    // 8..9: numEntries = 2
    // Entry 0 (12 bytes): Tag 0x0201 (JPEGInterchangeFormat), Type 4 (LONG), Count 1, Offset to JPEG
    // Entry 1 (12 bytes): Tag 0x0202 (JPEGInterchangeFormatLength), Type 4 (LONG), Count 1, Value = jpegBytes.size()
    // Next IFD (4 bytes): 0
    // JPEG data (starts at 8 + 2 + 12*2 + 4 = 38)
    const quint32 jpegOffset = 38;
    const quint32 jpegLength = static_cast<quint32>(jpegBytes.size());

    QByteArray tiffData;
    tiffData.append("II\x2A\x00", 4);
    const quint32 ifd0Offset = 8;
    tiffData.append(reinterpret_cast<const char*>(&ifd0Offset), 4);

    const quint16 numEntries = 2;
    tiffData.append(reinterpret_cast<const char*>(&numEntries), 2);

    // Entry 0: 0x0201
    const quint16 tag1 = 0x0201;
    const quint16 type1 = 4;
    const quint32 count1 = 1;
    tiffData.append(reinterpret_cast<const char*>(&tag1), 2);
    tiffData.append(reinterpret_cast<const char*>(&type1), 2);
    tiffData.append(reinterpret_cast<const char*>(&count1), 4);
    tiffData.append(reinterpret_cast<const char*>(&jpegOffset), 4);

    // Entry 1: 0x0202
    const quint16 tag2 = 0x0202;
    const quint16 type2 = 4;
    const quint32 count2 = 1;
    tiffData.append(reinterpret_cast<const char*>(&tag2), 2);
    tiffData.append(reinterpret_cast<const char*>(&type2), 2);
    tiffData.append(reinterpret_cast<const char*>(&count2), 4);
    tiffData.append(reinterpret_cast<const char*>(&jpegLength), 4);

    // Next IFD = 0
    const quint32 nextIfd = 0;
    tiffData.append(reinterpret_cast<const char*>(&nextIfd), 4);

    QCOMPARE(tiffData.size(), static_cast<int>(jpegOffset));
    tiffData.append(jpegBytes);

    const QString rawFile = QDir(m_tempDir->path()).filePath(QStringLiteral("sample.dng"));
    {
        QFile f(rawFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(tiffData);
        f.close();
    }

    const glancelot::RawPreviewResult result = glancelot::RawPreviewExtractor::extractPreview(rawFile);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.width, 320);
    QCOMPARE(result.height, 240);
    QCOMPARE(result.jpegData.size(), jpegBytes.size());
    QVERIFY(result.extractionTimeUs >= 0);

    // Verify JPEG decodes properly
    QImage testDecode;
    QVERIFY(testDecode.loadFromData(result.jpegData, "JPEG"));
    QCOMPARE(testDecode.width(), 320);
    QCOMPARE(testDecode.height(), 240);
}

void FormatTests::testRawPreviewFallbackScan() {
    // Create a mock raw file with 1024 bytes of dummy header followed by JPEG
    QByteArray jpegBytes;
    {
        QImage previewImg(200, 150, QImage::Format_RGB32);
        previewImg.fill(Qt::yellow);
        QBuffer buf(&jpegBytes);
        buf.open(QIODevice::WriteOnly);
        previewImg.save(&buf, "JPEG");
    }
    QVERIFY(!jpegBytes.isEmpty());

    QByteArray mockRaw;
    mockRaw.append(QByteArray(1024, 'R')); // non-TIFF header
    mockRaw.append(jpegBytes);

    const QString cr3File = QDir(m_tempDir->path()).filePath(QStringLiteral("sample.cr3"));
    {
        QFile f(cr3File);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(mockRaw);
        f.close();
    }

    const glancelot::RawPreviewResult result = glancelot::RawPreviewExtractor::extractPreview(cr3File);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.width, 200);
    QCOMPARE(result.height, 150);
}

void FormatTests::testRawAbsentPreview() {
    // Valid TIFF header with empty IFD (no JPEG preview)
    QByteArray emptyTiff;
    emptyTiff.append("II\x2A\x00", 4);
    const quint32 ifd0 = 8;
    emptyTiff.append(reinterpret_cast<const char*>(&ifd0), 4);
    const quint16 zeroEntries = 0;
    emptyTiff.append(reinterpret_cast<const char*>(&zeroEntries), 2);
    const quint32 nextIfd = 0;
    emptyTiff.append(reinterpret_cast<const char*>(&nextIfd), 4);
    emptyTiff.append(QByteArray(64, '\0')); // Ensure file size > 16

    const QString rawFile = QDir(m_tempDir->path()).filePath(QStringLiteral("nopreview.nef"));
    {
        QFile f(rawFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(emptyTiff);
        f.close();
    }

    const glancelot::RawPreviewResult result = glancelot::RawPreviewExtractor::extractPreview(rawFile);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("No embedded preview")));
}

void FormatTests::testMalformedRawInputs() {
    // 1. Zero-byte file
    const QString emptyFile = QDir(m_tempDir->path()).filePath(QStringLiteral("zero.arw"));
    {
        QFile f(emptyFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
    }
    const auto res1 = glancelot::RawPreviewExtractor::extractPreview(emptyFile);
    QVERIFY(!res1.success);

    // 2. Corrupt truncated header
    const QString truncFile = QDir(m_tempDir->path()).filePath(QStringLiteral("trunc.orf"));
    {
        QFile f(truncFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("II\x2A"); // only 3 bytes
        f.close();
    }
    const auto res2 = glancelot::RawPreviewExtractor::extractPreview(truncFile);
    QVERIFY(!res2.success);

    // 3. Random noise
    const QString noiseFile = QDir(m_tempDir->path()).filePath(QStringLiteral("noise.rw2"));
    {
        QFile f(noiseFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, '\xAA'));
        f.close();
    }
    const auto res3 = glancelot::RawPreviewExtractor::extractPreview(noiseFile);
    QVERIFY(!res3.success);
}

QTEST_MAIN(FormatTests)
#include "format_tests.moc"

