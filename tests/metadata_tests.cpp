#include <QTest>
#include "glancelot/metadata_model.hpp"
#include "glancelot/metadata_parser.hpp"
#include <QTemporaryDir>
#include <QImage>

class MetadataTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testBasicModelOperations();
    void testSearchAndCategoryFilter();
    void testParserWithSampleImage();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
};

void MetadataTests::initTestCase() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
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

QTEST_MAIN(MetadataTests)
#include "metadata_tests.moc"

