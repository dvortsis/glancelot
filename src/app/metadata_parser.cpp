#include "glancelot/metadata_parser.hpp"
#include <QFileInfo>
#include <QImageReader>
#include <QFile>
#include <QDateTime>
#include <cmath>

namespace glancelot {

MetadataRecord MetadataParser::parse(const QString& filePath, quint64 generation, qint64 maxReadBytes) {
    MetadataRecord record;
    record.generation = generation;
    record.filePath = filePath;

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        record.valid = false;
        return record;
    }

    record.valid = true;
    record.fileName = info.fileName();
    record.fileSize = info.size();

    QImageReader reader(filePath);
    const QByteArray format = reader.format();
    record.fileFormat = QString::fromLatin1(format).toUpper();

    const QSize size = reader.size();
    record.storedWidth = size.width();
    record.storedHeight = size.height();

    // Orientation-aware dimensions
    const auto transform = reader.transformation();
    if (transform == QImageIOHandler::TransformationRotate90 ||
        transform == QImageIOHandler::TransformationRotate270 ||
        transform == QImageIOHandler::TransformationFlipAndRotate90) {
        record.displayWidth = size.height();
        record.displayHeight = size.width();
    } else {
        record.displayWidth = size.width();
        record.displayHeight = size.height();
    }

    switch (transform) {
    case QImageIOHandler::TransformationNone:
        record.orientationCode = 1;
        record.orientationDescription = QStringLiteral("Normal (1)");
        break;
    case QImageIOHandler::TransformationMirror:
        record.orientationCode = 2;
        record.orientationDescription = QStringLiteral("Mirrored horizontal (2)");
        break;
    case QImageIOHandler::TransformationRotate180:
        record.orientationCode = 3;
        record.orientationDescription = QStringLiteral("Rotated 180° (3)");
        break;
    case QImageIOHandler::TransformationFlip:
        record.orientationCode = 4;
        record.orientationDescription = QStringLiteral("Mirrored vertical (4)");
        break;
    case QImageIOHandler::TransformationFlipAndRotate90:
        record.orientationCode = 5;
        record.orientationDescription = QStringLiteral("Mirrored & 270° (5)");
        break;
    case QImageIOHandler::TransformationRotate90:
        record.orientationCode = 6;
        record.orientationDescription = QStringLiteral("Rotated 90° CW (6)");
        break;
    case QImageIOHandler::TransformationMirrorAndRotate90:
        record.orientationCode = 7;
        record.orientationDescription = QStringLiteral("Mirrored & 90° CW (7)");
        break;
    case QImageIOHandler::TransformationRotate270:
        record.orientationCode = 8;
        record.orientationDescription = QStringLiteral("Rotated 270° CW (8)");
        break;
    }

    // Default bit depth & color space
    record.bitDepth = 8;
    record.colorSpace = QStringLiteral("sRGB");

    // Basic summary item
    record.items.append(MetadataItem{
        QStringLiteral("File.Name"),
        QStringLiteral("File Name"),
        record.fileName,
        record.fileName,
        QStringLiteral("Summary")
    });

    record.items.append(MetadataItem{
        QStringLiteral("File.Size"),
        QStringLiteral("File Size"),
        QString::number(record.fileSize) + QStringLiteral(" bytes"),
        QString::number(record.fileSize),
        QStringLiteral("Summary")
    });

    if (record.storedWidth > 0 && record.storedHeight > 0) {
        record.items.append(MetadataItem{
            QStringLiteral("Image.Dimensions"),
            QStringLiteral("Dimensions"),
            QString::number(record.displayWidth) + QStringLiteral(" × ") + QString::number(record.displayHeight),
            QString::number(record.storedWidth) + QStringLiteral("x") + QString::number(record.storedHeight),
            QStringLiteral("Image")
        });
    }

    return record;
}

} // namespace glancelot

