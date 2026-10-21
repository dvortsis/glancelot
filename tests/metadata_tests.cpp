#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QBuffer>
#include "glancelot/metadata_model.hpp"
#include "glancelot/metadata_parser.hpp"

class MetadataTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testBasicModelOperations();
    void testSearchAndCategoryFilter();
    void testParserWithSampleImage();
    void testValidJpegExif();
    void testGpsMetadataAndFormatting();
    void testIptcMetadataExtraction();
    void testMalformedHeaderResilience();
    void testCircularIfdLoopPrevention();
    void testOrientationMapping();
    void testClipboardHelpers();
    void testGenerationValidation();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;

    QByteArray createJpegWithExif(const QString& make, const QString& model, int iso,
                                 double exposureTime, double fNumber, double focalLength,
                                 const QString& dateTime, int orientation = 1);
    QByteArray createJpegWithGps(double lat, double lon, double alt);
    QByteArray createJpegWithIptc(const QString& title, const QString& author, const QString& copyright);
};

void MetadataTests::initTestCase() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
}

QByteArray MetadataTests::createJpegWithExif(const QString& make, const QString& model, int iso,
                                             double exposureTime, double fNumber, double focalLength,
                                             const QString& dateTime, int orientation) {
    QByteArray tiff;
    QDataStream ds(&tiff, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    // TIFF Header (8 bytes)
    ds << quint8('I') << quint8('I');
    ds << quint16(42);
    ds << quint32(8); // Offset to IFD0

    // Offsets calculation
    // IFD0: 2 (count) + 4 entries * 12 + 4 (next) = 54 bytes
    // So IFD0 ends at 8 + 54 = 62.
    // Exif SubIFD will start at offset 62.
    const quint32 exifIfdOffset = 62;

    // IFD0 has 4 entries: Make, Model, Orientation, ExifIFD pointer
    ds << quint16(4); // entry count

    // Tag 0x0112: Orientation (SHORT, count 1, value)
    ds << quint16(0x0112) << quint16(3) << quint32(1) << quint32(orientation);

    // Value data area starts after Exif SubIFD
    // Exif SubIFD: 2 (count) + 5 entries * 12 + 4 (next) = 66 bytes.
    // String data starts at 62 + 66 = 128.
    quint32 strOffset = 128;

    // Tag 0x010F: Make (ASCII)
    const QByteArray makeBytes = make.toUtf8() + '\0';
    ds << quint16(0x010F) << quint16(2) << quint32(makeBytes.size()) << quint32(strOffset);
    strOffset += makeBytes.size();

    // Tag 0x0110: Model (ASCII)
    const QByteArray modelBytes = model.toUtf8() + '\0';
    ds << quint16(0x0110) << quint16(2) << quint32(modelBytes.size()) << quint32(strOffset);
    strOffset += modelBytes.size();

    // Tag 0x8769: ExifIFD Pointer (LONG)
    ds << quint16(0x8769) << quint16(4) << quint32(1) << quint32(exifIfdOffset);
    ds << quint32(0); // Next IFD = 0

    // --- Exif SubIFD at offset 62 ---
    ds << quint16(5); // 5 entries

    // Tag 0x8827: ISO (SHORT)
    ds << quint16(0x8827) << quint16(3) << quint32(1) << quint32(iso);

    // Rationals data area starts at strOffset
    quint32 rationalOffset = strOffset;
    const QByteArray dateBytes = dateTime.toUtf8() + '\0';
    rationalOffset += dateBytes.size();

    // Tag 0x829A: ExposureTime (RATIONAL, 8 bytes)
    ds << quint16(0x829A) << quint16(5) << quint32(1) << quint32(rationalOffset);
    rationalOffset += 8;

    // Tag 0x829D: FNumber (RATIONAL, 8 bytes)
    ds << quint16(0x829D) << quint16(5) << quint32(1) << quint32(rationalOffset);
    rationalOffset += 8;

    // Tag 0x920A: FocalLength (RATIONAL, 8 bytes)
    ds << quint16(0x920A) << quint16(5) << quint32(1) << quint32(rationalOffset);
    rationalOffset += 8;

    // Tag 0x9003: DateTimeOriginal (ASCII)
    ds << quint16(0x9003) << quint16(2) << quint32(dateBytes.size()) << quint32(strOffset);

    ds << quint32(0); // Next SubIFD = 0

    // Write String data
    ds.writeRawData(makeBytes.constData(), makeBytes.size());
    ds.writeRawData(modelBytes.constData(), modelBytes.size());
    ds.writeRawData(dateBytes.constData(), dateBytes.size());

    // Write Rational data
    // ExposureTime: e.g. 1 / (1/exposureTime)
    quint32 expNum = 1;
    quint32 expDen = exposureTime > 0 ? static_cast<quint32>(std::round(1.0 / exposureTime)) : 100;
    ds << expNum << expDen;

    // FNumber: e.g. (fNumber * 10) / 10
    quint32 fNum = static_cast<quint32>(std::round(fNumber * 10.0));
    quint32 fDen = 10;
    ds << fNum << fDen;

    // FocalLength: e.g. (focalLength * 10) / 10
    quint32 flNum = static_cast<quint32>(std::round(focalLength * 10.0));
    quint32 flDen = 10;
    ds << flNum << flDen;

    // Build JPEG stream
    QByteArray jpeg;
    jpeg.append("\xFF\xD8", 2); // SOI
    jpeg.append("\xFF\xE1", 2); // APP1
    const quint16 app1Len = static_cast<quint16>(2 + 6 + tiff.size());
    jpeg.append(static_cast<char>((app1Len >> 8) & 0xFF));
    jpeg.append(static_cast<char>(app1Len & 0xFF));
    jpeg.append("Exif\0\0", 6);
    jpeg.append(tiff);
    jpeg.append("\xFF\xD9", 2); // EOI

    return jpeg;
}

QByteArray MetadataTests::createJpegWithGps(double lat, double lon, double alt) {
    QByteArray tiff;
    QDataStream ds(&tiff, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    // TIFF Header
    ds << quint8('I') << quint8('I') << quint16(42) << quint32(8);

    // IFD0 with 1 entry: GPSInfo pointer (0x8825)
    ds << quint16(1);
    const quint32 gpsIfdOffset = 26; // 8 + 2 + 12 + 4
    ds << quint16(0x8825) << quint16(4) << quint32(1) << quint32(gpsIfdOffset);
    ds << quint32(0);

    // GPS IFD at offset 26
    ds << quint16(5); // 5 entries: LatRef, Lat, LonRef, Lon, Alt
    quint32 dataOffset = 26 + 2 + 5 * 12 + 4; // 92

    // Tag 0x0001: GPSLatitudeRef
    const char latRef = lat >= 0.0 ? 'N' : 'S';
    ds << quint16(0x0001) << quint16(2) << quint32(2) << quint32(latRef);

    // Tag 0x0002: GPSLatitude (3 rationals = 24 bytes)
    ds << quint16(0x0002) << quint16(5) << quint32(3) << quint32(dataOffset);
    dataOffset += 24;

    // Tag 0x0003: GPSLongitudeRef
    const char lonRef = lon >= 0.0 ? 'E' : 'W';
    ds << quint16(0x0003) << quint16(2) << quint32(2) << quint32(lonRef);

    // Tag 0x0004: GPSLongitude (3 rationals = 24 bytes)
    ds << quint16(0x0004) << quint16(5) << quint32(3) << quint32(dataOffset);
    dataOffset += 24;

    // Tag 0x0006: GPSAltitude (1 rational = 8 bytes)
    ds << quint16(0x0006) << quint16(5) << quint32(1) << quint32(dataOffset);

    ds << quint32(0); // End of GPS IFD

    // Data values:
    double absLat = std::abs(lat);
    quint32 latDeg = static_cast<quint32>(absLat);
    double latRemMin = (absLat - latDeg) * 60.0;
    quint32 latMin = static_cast<quint32>(latRemMin);
    quint32 latSec = static_cast<quint32>(std::round((latRemMin - latMin) * 60.0 * 10.0));
    ds << latDeg << quint32(1) << latMin << quint32(1) << latSec << quint32(10);

    double absLon = std::abs(lon);
    quint32 lonDeg = static_cast<quint32>(absLon);
    double lonRemMin = (absLon - lonDeg) * 60.0;
    quint32 lonMin = static_cast<quint32>(lonRemMin);
    quint32 lonSec = static_cast<quint32>(std::round((lonRemMin - lonMin) * 60.0 * 10.0));
    ds << lonDeg << quint32(1) << lonMin << quint32(1) << lonSec << quint32(10);

    ds << static_cast<quint32>(std::round(alt * 10.0)) << quint32(10);

    // Wrap in JPEG APP1
    QByteArray jpeg;
    jpeg.append("\xFF\xD8\xFF\xE1", 4);
    const quint16 len = static_cast<quint16>(2 + 6 + tiff.size());
    jpeg.append(static_cast<char>((len >> 8) & 0xFF));
    jpeg.append(static_cast<char>(len & 0xFF));
    jpeg.append("Exif\0\0", 6);
    jpeg.append(tiff);
    jpeg.append("\xFF\xD9", 2);

    return jpeg;
}

QByteArray MetadataTests::createJpegWithIptc(const QString& title, const QString& author, const QString& copyright) {
    QByteArray iptc;
    // Title (Dataset 2:05)
    const QByteArray titleBytes = title.toUtf8();
    iptc.append(char(0x1C)); iptc.append(char(2)); iptc.append(char(5));
    iptc.append(char((titleBytes.size() >> 8) & 0xFF)); iptc.append(char(titleBytes.size() & 0xFF));
    iptc.append(titleBytes);

    // Author (Dataset 2:80)
    const QByteArray authorBytes = author.toUtf8();
    iptc.append(char(0x1C)); iptc.append(char(2)); iptc.append(char(80));
    iptc.append(char((authorBytes.size() >> 8) & 0xFF)); iptc.append(char(authorBytes.size() & 0xFF));
    iptc.append(authorBytes);

    // Copyright (Dataset 2:116)
    const QByteArray copyBytes = copyright.toUtf8();
    iptc.append(char(0x1C)); iptc.append(char(2)); iptc.append(char(116));
    iptc.append(char((copyBytes.size() >> 8) & 0xFF)); iptc.append(char(copyBytes.size() & 0xFF));
    iptc.append(copyBytes);

    // Wrap in Photoshop 3.0 APP13
    QByteArray app13;
    app13.append("Photoshop 3.0\0", 14);
    app13.append("8BIM", 4);
    app13.append(char(0x04)); app13.append(char(0x04)); // IPTC-NAA
    app13.append(char(0)); app13.append(char(0)); // Name string (empty)
    app13.append(char((iptc.size() >> 24) & 0xFF));
    app13.append(char((iptc.size() >> 16) & 0xFF));
    app13.append(char((iptc.size() >> 8) & 0xFF));
    app13.append(char(iptc.size() & 0xFF));
    app13.append(iptc);

    QByteArray jpeg;
    jpeg.append("\xFF\xD8\xFF\xED", 4);
    const quint16 len = static_cast<quint16>(2 + app13.size());
    jpeg.append(static_cast<char>((len >> 8) & 0xFF));
    jpeg.append(static_cast<char>(len & 0xFF));
    jpeg.append(app13);
    jpeg.append("\xFF\xD9", 2);

    return jpeg;
}

void MetadataTests::testBasicModelOperations() {
    MetadataModel model;
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.loading(), false);
    QCOMPARE(model.hasMetadata(), false);

    glancelot::MetadataRecord record;
    record.valid = true;
    record.fileName = QStringLiteral("test.jpg");
    record.cameraMake = QStringLiteral("Nikon");
    record.cameraModel = QStringLiteral("Z8");
    record.displayWidth = 8256;
    record.displayHeight = 5504;
    record.items.append(glancelot::MetadataItem{
        QStringLiteral("Exif.Image.Make"),
        QStringLiteral("Make"),
        QStringLiteral("Nikon"),
        QStringLiteral("Nikon"),
        QStringLiteral("Camera")
    });

    model.setRecord(record);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.hasMetadata(), true);
    QCOMPARE(model.cameraSummary(), QStringLiteral("Nikon Z8"));

    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.hasMetadata(), false);
}

void MetadataTests::testSearchAndCategoryFilter() {
    MetadataModel model;
    glancelot::MetadataRecord record;
    record.valid = true;
    record.items.append(glancelot::MetadataItem{
        QStringLiteral("Make"), QStringLiteral("Camera Make"), QStringLiteral("Sony"), QStringLiteral("Sony"), QStringLiteral("Camera")
    });
    record.items.append(glancelot::MetadataItem{
        QStringLiteral("FocalLength"), QStringLiteral("Focal Length"), QStringLiteral("50mm"), QStringLiteral("50/1"), QStringLiteral("Camera")
    });
    record.items.append(glancelot::MetadataItem{
        QStringLiteral("GPSLatitude"), QStringLiteral("Latitude"), QStringLiteral("37.77"), QStringLiteral("37.77"), QStringLiteral("GPS")
    });

    model.setRecord(record);
    QCOMPARE(model.count(), 3);

    model.setCategoryFilter(QStringLiteral("GPS"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), MetadataModel::KeyRole).toString(), QStringLiteral("GPSLatitude"));

    model.setCategoryFilter(QStringLiteral("All"));
    QCOMPARE(model.count(), 3);

    model.setSearchFilter(QStringLiteral("50mm"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), MetadataModel::KeyRole).toString(), QStringLiteral("FocalLength"));
}

void MetadataTests::testParserWithSampleImage() {
    const QString imgPath = tempDir_->filePath("sample.png");
    QImage testImg(100, 80, QImage::Format_RGB32);
    testImg.fill(Qt::blue);
    QVERIFY(testImg.save(imgPath, "PNG"));

    const auto record = glancelot::MetadataParser::parse(imgPath, 1);
    QVERIFY(record.valid);
    QCOMPARE(record.storedWidth, 100);
    QCOMPARE(record.storedHeight, 80);
    QCOMPARE(record.displayWidth, 100);
    QCOMPARE(record.displayHeight, 80);
    QCOMPARE(record.fileFormat, QStringLiteral("PNG"));
}

void MetadataTests::testValidJpegExif() {
    const QByteArray jpeg = createJpegWithExif(
        QStringLiteral("Sony"), QStringLiteral("ILCE-7RM5"),
        400, 0.004 /* 1/250s */, 2.8, 50.0,
        QStringLiteral("2026:10:18 15:30:00"), 1);

    const QString path = tempDir_->filePath("sony_a7r5.jpg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(jpeg);
    f.close();

    const auto record = glancelot::MetadataParser::parse(path, 10);
    QVERIFY(record.valid);
    QCOMPARE(record.cameraMake, QStringLiteral("Sony"));
    QCOMPARE(record.cameraModel, QStringLiteral("ILCE-7RM5"));
    QCOMPARE(record.isoRating, QStringLiteral("400"));
    QCOMPARE(record.exposureTime, QStringLiteral("1/250s"));
    QCOMPARE(record.fNumber, QStringLiteral("f/2.8"));
    QCOMPARE(record.focalLength, QStringLiteral("50.0 mm"));
    QCOMPARE(record.dateTimeOriginal, QStringLiteral("2026:10:18 15:30:00"));
    QCOMPARE(record.orientationCode, 1);
}

void MetadataTests::testGpsMetadataAndFormatting() {
    const QByteArray jpeg = createJpegWithGps(37.774929, -122.419416, 15.5);
    const QString path = tempDir_->filePath("gps_photo.jpg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(jpeg);
    f.close();

    const auto record = glancelot::MetadataParser::parse(path, 11);
    QVERIFY(record.valid);
    QVERIFY(record.hasGps);
    QVERIFY(std::abs(record.latitude - 37.7749) < 0.01);
    QVERIFY(std::abs(record.longitude - (-122.4194)) < 0.01);
    QVERIFY(record.gpsLatitudeStr.contains("N"));
    QVERIFY(record.gpsLongitudeStr.contains("W"));
    QCOMPARE(record.gpsAltitudeStr, QStringLiteral("15.5 m"));
}

void MetadataTests::testIptcMetadataExtraction() {
    const QByteArray jpeg = createJpegWithIptc(
        QStringLiteral("Golden Gate at Sunset"),
        QStringLiteral("Ansel Adams"),
        QStringLiteral("Copyright 2026 All Rights Reserved"));

    const QString path = tempDir_->filePath("iptc_photo.jpg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(jpeg);
    f.close();

    const auto record = glancelot::MetadataParser::parse(path, 12);
    QVERIFY(record.valid);
    QCOMPARE(record.iptcTitle, QStringLiteral("Golden Gate at Sunset"));
    QCOMPARE(record.iptcCreator, QStringLiteral("Ansel Adams"));
    QCOMPARE(record.iptcCopyright, QStringLiteral("Copyright 2026 All Rights Reserved"));
}

void MetadataTests::testMalformedHeaderResilience() {
    // 1. Corrupted JPEG with truncated APP1
    QByteArray corrupted;
    corrupted.append("\xFF\xD8\xFF\xE1\x00\x10", 6);
    corrupted.append("Exif\0\0II\x2a\x00\xFF\xFF", 12);
    const QString path1 = tempDir_->filePath("corrupt1.jpg");
    QFile f1(path1);
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write(corrupted);
    f1.close();

    // Must not crash or throw
    const auto record1 = glancelot::MetadataParser::parse(path1, 13);
    QVERIFY(record1.valid);

    // 2. Pure garbage file
    QByteArray garbage(1024, '\xDE');
    const QString path2 = tempDir_->filePath("garbage.dat");
    QFile f2(path2);
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write(garbage);
    f2.close();

    const auto record2 = glancelot::MetadataParser::parse(path2, 14);
    QVERIFY(record2.valid);
}

void MetadataTests::testCircularIfdLoopPrevention() {
    // Construct TIFF header with an IFD pointing back to offset 8 (itself)
    QByteArray tiff;
    QDataStream ds(&tiff, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    ds << quint8('I') << quint8('I') << quint16(42) << quint32(8);
    // IFD0 at offset 8 with 1 entry pointing to SubIFD at offset 8!
    ds << quint16(1);
    ds << quint16(0x8769) << quint16(4) << quint32(1) << quint32(8); // Loop back!
    ds << quint32(8); // Next IFD points back to 8!

    QByteArray jpeg;
    jpeg.append("\xFF\xD8\xFF\xE1", 4);
    const quint16 len = static_cast<quint16>(2 + 6 + tiff.size());
    jpeg.append(static_cast<char>((len >> 8) & 0xFF));
    jpeg.append(static_cast<char>(len & 0xFF));
    jpeg.append("Exif\0\0", 6);
    jpeg.append(tiff);
    jpeg.append("\xFF\xD9", 2);

    const QString path = tempDir_->filePath("loop.jpg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(jpeg);
    f.close();

    // Must safely detect loop and return without hanging or blowing the stack
    const auto record = glancelot::MetadataParser::parse(path, 15);
    QVERIFY(record.valid);
}

void MetadataTests::testOrientationMapping() {
    // Orientation 6: Rotated 90° CW
    const QByteArray jpeg = createJpegWithExif(
        QStringLiteral("Apple"), QStringLiteral("iPhone 15"),
        50, 0.01, 1.8, 24.0, QStringLiteral("2026:10:19 12:00:00"), 6);

    const QString path = tempDir_->filePath("orient6.jpg");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(jpeg);
    f.close();

    const auto record = glancelot::MetadataParser::parse(path, 16);
    QVERIFY(record.valid);
    QCOMPARE(record.orientationCode, 6);
    QVERIFY(record.orientationDescription.contains("90") && record.orientationDescription.contains("CW"));
}

void MetadataTests::testClipboardHelpers() {
    MetadataModel model;
    glancelot::MetadataRecord record;
    record.valid = true;
    record.fileName = QStringLiteral("IMG_0042.JPG");
    record.cameraMake = QStringLiteral("Canon");
    record.cameraModel = QStringLiteral("EOS R5");
    record.lensModel = QStringLiteral("RF 24-70mm F2.8L IS USM");
    record.exposureTime = QStringLiteral("1/500s");
    record.fNumber = QStringLiteral("f/2.8");
    record.isoRating = QStringLiteral("100");
    record.focalLength = QStringLiteral("70.0 mm");
    record.displayWidth = 8192;
    record.displayHeight = 5464;
    record.hasGps = true;
    record.latitude = 37.7749;
    record.longitude = -122.4194;
    record.gpsLatitudeStr = QStringLiteral("37° 46' 29.6\" N");
    record.gpsLongitudeStr = QStringLiteral("122° 25' 9.8\" W");
    record.items.append(glancelot::MetadataItem{
        QStringLiteral("Exif.Photo.FNumber"), QStringLiteral("Aperture"), QStringLiteral("f/2.8"), QStringLiteral("2.8"), QStringLiteral("Camera")
    });

    model.setRecord(record);

    const QString summary = model.copySummary();
    QVERIFY(summary.contains("IMG_0042.JPG"));
    QVERIFY(summary.contains("Canon EOS R5"));
    QVERIFY(summary.contains("1/500s"));
    QVERIFY(summary.contains("37° 46' 29.6\" N"));

    const QString gps = model.copyGps();
    QVERIFY(gps.contains("37° 46' 29.6\" N"));

    const QString tag = model.copyTag(QStringLiteral("Exif.Photo.FNumber"));
    QCOMPARE(tag, QStringLiteral("f/2.8"));
}

void MetadataTests::testGenerationValidation() {
    MetadataModel model;
    QCOMPARE(model.loading(), false);

    model.setLoading(true);
    QCOMPARE(model.loading(), true);

    glancelot::MetadataRecord rec1;
    rec1.generation = 1;
    rec1.cameraModel = QStringLiteral("Older Model");
    model.setRecord(rec1);
    QCOMPARE(model.loading(), false);
    QCOMPARE(model.cameraSummary(), QStringLiteral("Older Model"));

    glancelot::MetadataRecord rec2;
    rec2.generation = 2;
    rec2.cameraModel = QStringLiteral("Newer Model");
    model.setRecord(rec2);
    QCOMPARE(model.cameraSummary(), QStringLiteral("Newer Model"));
}

QTEST_MAIN(MetadataTests)
#include "metadata_tests.moc"
