#include "glancelot/metadata_parser.hpp"
#include "glancelot/io_scheduler.hpp"
#include "shared_file_read.hpp"
#include <QFileInfo>
#include <QImageReader>
#include <QFile>
#include <QDateTime>
#include <QSet>
#include <QXmlStreamReader>
#include <cmath>
#include <cstring>

namespace glancelot {

namespace {

// Safe binary reading helpers with strict bounds checking.
inline quint16 readU16LE(const uchar* p) {
    return static_cast<quint16>(p[0] | (static_cast<quint16>(p[1]) << 8));
}

inline quint16 readU16BE(const uchar* p) {
    return static_cast<quint16>((static_cast<quint16>(p[0]) << 8) | p[1]);
}

inline quint32 readU32LE(const uchar* p) {
    return static_cast<quint32>(p[0] | (static_cast<quint32>(p[1]) << 8) |
                               (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24));
}

inline quint32 readU32BE(const uchar* p) {
    return static_cast<quint32>((static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
                               (static_cast<quint32>(p[2]) << 8) | p[3]);
}

inline qint32 readS32LE(const uchar* p) {
    return static_cast<qint32>(readU32LE(p));
}

inline qint32 readS32BE(const uchar* p) {
    return static_cast<qint32>(readU32BE(p));
}

struct TiffReader {
    const uchar* data = nullptr;
    int size = 0;
    bool isLittleEndian = true;

    bool canRead(int offset, int len) const {
        return offset >= 0 && len >= 0 && offset + len <= size;
    }

    quint16 u16(int offset) const {
        if (!canRead(offset, 2)) return 0;
        return isLittleEndian ? readU16LE(data + offset) : readU16BE(data + offset);
    }

    quint32 u32(int offset) const {
        if (!canRead(offset, 4)) return 0;
        return isLittleEndian ? readU32LE(data + offset) : readU32BE(data + offset);
    }

    qint32 s32(int offset) const {
        if (!canRead(offset, 4)) return 0;
        return isLittleEndian ? readS32LE(data + offset) : readS32BE(data + offset);
    }

    double rational(int offset) const {
        if (!canRead(offset, 8)) return 0.0;
        const quint32 num = u32(offset);
        const quint32 den = u32(offset + 4);
        if (den == 0) return 0.0;
        return static_cast<double>(num) / static_cast<double>(den);
    }

    double srational(int offset) const {
        if (!canRead(offset, 8)) return 0.0;
        const qint32 num = s32(offset);
        const qint32 den = s32(offset + 4);
        if (den == 0) return 0.0;
        return static_cast<double>(num) / static_cast<double>(den);
    }

    QString string(int offset, int count) const {
        if (count <= 0 || !canRead(offset, count)) return {};
        // Find NUL terminator or cap at count (max 4096)
        int len = std::min(count, 4096);
        for (int i = 0; i < len; ++i) {
            if (data[offset + i] == '\0') {
                len = i;
                break;
            }
        }
        return QString::fromUtf8(reinterpret_cast<const char*>(data + offset), len).trimmed();
    }
};

void setRecordOrientation(MetadataRecord& record, int code) {
    record.orientationCode = code;
    switch (code) {
    case 1: record.orientationDescription = QStringLiteral("Normal (1)"); break;
    case 2: record.orientationDescription = QStringLiteral("Mirrored horizontal (2)"); break;
    case 3: record.orientationDescription = QStringLiteral("Rotated 180° (3)"); break;
    case 4: record.orientationDescription = QStringLiteral("Mirrored vertical (4)"); break;
    case 5: record.orientationDescription = QStringLiteral("Mirrored & 270° CW (5)"); break;
    case 6: record.orientationDescription = QStringLiteral("Rotated 90° CW (6)"); break;
    case 7: record.orientationDescription = QStringLiteral("Mirrored & 90° CW (7)"); break;
    case 8: record.orientationDescription = QStringLiteral("Rotated 270° CW (8)"); break;
    default: record.orientationDescription = QStringLiteral("Unknown"); break;
    }
    if (code == 5 || code == 6 || code == 7 || code == 8) {
        if (record.storedWidth > 0 && record.storedHeight > 0 &&
            record.displayWidth == record.storedWidth && record.displayHeight == record.storedHeight) {
            record.displayWidth = record.storedHeight;
            record.displayHeight = record.storedWidth;
        }
    }
}

void parseTiffIFD(const TiffReader& tiff, int ifdOffset, MetadataRecord& record,
                  QSet<quint32>& visited, int depth, const QString& parentCategory = QStringLiteral("Camera")) {
    if (depth > 16 || ifdOffset <= 0 || !tiff.canRead(ifdOffset, 2)) return;
    if (visited.contains(static_cast<quint32>(ifdOffset))) return; // Cycle protection
    visited.insert(static_cast<quint32>(ifdOffset));

    const quint16 numEntries = tiff.u16(ifdOffset);
    int entryOffset = ifdOffset + 2;

    for (int i = 0; i < numEntries; ++i) {
        if (!tiff.canRead(entryOffset, 12)) break;

        const quint16 tag = tiff.u16(entryOffset);
        const quint16 type = tiff.u16(entryOffset + 2);
        const quint32 count = tiff.u32(entryOffset + 4);

        int valueOffset = entryOffset + 8;
        // Determine type size
        int typeSize = 1;
        switch (type) {
        case 1: case 2: case 7: typeSize = 1; break; // BYTE, ASCII, UNDEFINED
        case 3: case 8: typeSize = 2; break;         // SHORT, SSHORT
        case 4: case 9: case 11: typeSize = 4; break;// LONG, SLONG, FLOAT
        case 5: case 10: case 12: typeSize = 8; break;// RATIONAL, SRATIONAL, DOUBLE
        default: typeSize = 1; break;
        }

        const quint32 totalBytes = count * typeSize;
        if (totalBytes > 4) {
            valueOffset = static_cast<int>(tiff.u32(entryOffset + 8));
        }

        if (tiff.canRead(valueOffset, static_cast<int>(totalBytes))) {
            // Process known tags
            switch (tag) {
            case 0x010F: // Make
                record.cameraMake = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.Make"), QStringLiteral("Camera Make"), record.cameraMake, record.cameraMake, QStringLiteral("Camera")});
                break;
            case 0x0110: // Model
                record.cameraModel = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.Model"), QStringLiteral("Camera Model"), record.cameraModel, record.cameraModel, QStringLiteral("Camera")});
                break;
            case 0x0112: { // Orientation
                const quint16 ori = tiff.u16(valueOffset);
                if (ori >= 1 && ori <= 8) {
                    setRecordOrientation(record, ori);
                    record.items.append(MetadataItem{QStringLiteral("Exif.Image.Orientation"), QStringLiteral("Orientation"), record.orientationDescription, QString::number(ori), QStringLiteral("Image")});
                }
                break;
            }
            case 0x0131: { // Software
                const QString sw = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.Software"), QStringLiteral("Software"), sw, sw, QStringLiteral("Summary")});
                break;
            }
            case 0x0132: { // DateTime
                const QString dt = tiff.string(valueOffset, count);
                if (record.dateTimeOriginal.isEmpty()) record.dateTimeOriginal = dt;
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.DateTime"), QStringLiteral("Date / Time"), dt, dt, QStringLiteral("Summary")});
                break;
            }
            case 0x013B: { // Artist
                const QString artist = tiff.string(valueOffset, count);
                if (record.iptcCreator.isEmpty()) record.iptcCreator = artist;
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.Artist"), QStringLiteral("Artist"), artist, artist, QStringLiteral("IPTC")});
                break;
            }
            case 0x8298: { // Copyright
                const QString copyright = tiff.string(valueOffset, count);
                if (record.iptcCopyright.isEmpty()) record.iptcCopyright = copyright;
                record.items.append(MetadataItem{QStringLiteral("Exif.Image.Copyright"), QStringLiteral("Copyright"), copyright, copyright, QStringLiteral("IPTC")});
                break;
            }
            case 0x8769: { // ExifIFD pointer
                const quint32 subOffset = tiff.u32(valueOffset);
                parseTiffIFD(tiff, static_cast<int>(subOffset), record, visited, depth + 1, QStringLiteral("Camera"));
                break;
            }
            case 0x8825: { // GPSInfo IFD pointer
                const quint32 gpsOffset = tiff.u32(valueOffset);
                parseTiffIFD(tiff, static_cast<int>(gpsOffset), record, visited, depth + 1, QStringLiteral("GPS"));
                break;
            }
            // --- Exif SubIFD Tags ---
            case 0x829A: { // ExposureTime
                const double val = tiff.rational(valueOffset);
                if (val > 0.0) {
                    if (val < 1.0) {
                        const int denom = static_cast<int>(std::round(1.0 / val));
                        record.exposureTime = QString::asprintf("1/%ds", denom);
                    } else {
                        record.exposureTime = QString::asprintf("%.1fs", val);
                    }
                    record.items.append(MetadataItem{QStringLiteral("Exif.Photo.ExposureTime"), QStringLiteral("Shutter Speed"), record.exposureTime, QString::number(val), QStringLiteral("Camera")});
                }
                break;
            }
            case 0x829D: { // FNumber
                const double val = tiff.rational(valueOffset);
                if (val > 0.0) {
                    record.fNumber = QString::asprintf("f/%.1f", val);
                    record.items.append(MetadataItem{QStringLiteral("Exif.Photo.FNumber"), QStringLiteral("Aperture"), record.fNumber, QString::number(val), QStringLiteral("Camera")});
                }
                break;
            }
            case 0x8822: { // ExposureProgram
                const quint16 prog = tiff.u16(valueOffset);
                const char* desc = "Unknown";
                switch (prog) {
                case 1: desc = "Manual"; break;
                case 2: desc = "Normal Program (Auto)"; break;
                case 3: desc = "Aperture Priority"; break;
                case 4: desc = "Shutter Priority"; break;
                case 5: desc = "Creative (Depth of Field)"; break;
                case 6: desc = "Action (High Shutter)"; break;
                case 7: desc = "Portrait Mode"; break;
                case 8: desc = "Landscape Mode"; break;
                }
                record.exposureProgram = QString::fromLatin1(desc);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.ExposureProgram"), QStringLiteral("Exposure Program"), record.exposureProgram, QString::number(prog), QStringLiteral("Camera")});
                break;
            }
            case 0x8827: { // PhotographicSensitivity / ISO
                const quint16 iso = tiff.u16(valueOffset);
                record.isoRating = QString::number(iso);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.ISOSpeedRatings"), QStringLiteral("ISO Speed"), record.isoRating, QString::number(iso), QStringLiteral("Camera")});
                break;
            }
            case 0x9003: { // DateTimeOriginal
                record.dateTimeOriginal = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.DateTimeOriginal"), QStringLiteral("Date Captured"), record.dateTimeOriginal, record.dateTimeOriginal, QStringLiteral("Summary")});
                break;
            }
            case 0x9004: { // DateTimeDigitized
                record.dateTimeDigitized = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.DateTimeDigitized"), QStringLiteral("Date Digitized"), record.dateTimeDigitized, record.dateTimeDigitized, QStringLiteral("Summary")});
                break;
            }
            case 0x9204: { // ExposureBiasValue
                const double val = tiff.srational(valueOffset);
                const QString formatted = QString::asprintf("%+.1f EV", val);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.ExposureBiasValue"), QStringLiteral("Exposure Bias"), formatted, QString::number(val), QStringLiteral("Camera")});
                break;
            }
            case 0x9207: { // MeteringMode
                const quint16 mode = tiff.u16(valueOffset);
                const char* desc = "Unknown";
                switch (mode) {
                case 1: desc = "Average"; break;
                case 2: desc = "Center-Weighted Average"; break;
                case 3: desc = "Spot"; break;
                case 4: desc = "Multi-Spot"; break;
                case 5: desc = "Pattern / Multi-Segment"; break;
                case 6: desc = "Partial"; break;
                }
                record.meteringMode = QString::fromLatin1(desc);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.MeteringMode"), QStringLiteral("Metering Mode"), record.meteringMode, QString::number(mode), QStringLiteral("Camera")});
                break;
            }
            case 0x9209: { // Flash
                const quint16 flash = tiff.u16(valueOffset);
                const bool fired = (flash & 0x01) != 0;
                record.flash = fired ? QStringLiteral("Flash fired") : QStringLiteral("Flash did not fire");
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.Flash"), QStringLiteral("Flash"), record.flash, QString::number(flash), QStringLiteral("Camera")});
                break;
            }
            case 0x920A: { // FocalLength
                const double val = tiff.rational(valueOffset);
                if (val > 0.0) {
                    record.focalLength = QString::asprintf("%.1f mm", val);
                    record.items.append(MetadataItem{QStringLiteral("Exif.Photo.FocalLength"), QStringLiteral("Focal Length"), record.focalLength, QString::number(val), QStringLiteral("Camera")});
                }
                break;
            }
            case 0xA001: { // ColorSpace
                const quint16 cs = tiff.u16(valueOffset);
                if (cs == 1) record.colorSpace = QStringLiteral("sRGB");
                else if (cs == 0xFFFF || cs == 2) record.colorSpace = QStringLiteral("Adobe RGB");
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.ColorSpace"), QStringLiteral("Color Space"), record.colorSpace, QString::number(cs), QStringLiteral("Image")});
                break;
            }
            case 0xA405: { // FocalLengthIn35mmFilm
                const quint16 fl35 = tiff.u16(valueOffset);
                if (fl35 > 0) {
                    record.focalLength35mm = QString::number(fl35) + QStringLiteral(" mm");
                    record.items.append(MetadataItem{QStringLiteral("Exif.Photo.FocalLengthIn35mmFilm"), QStringLiteral("35mm Equivalent"), record.focalLength35mm, QString::number(fl35), QStringLiteral("Camera")});
                }
                break;
            }
            case 0xA434: { // LensModel
                record.lensModel = tiff.string(valueOffset, count);
                record.items.append(MetadataItem{QStringLiteral("Exif.Photo.LensModel"), QStringLiteral("Lens Model"), record.lensModel, record.lensModel, QStringLiteral("Camera")});
                break;
            }
            // --- GPS Tags ---
            case 0x0001: { // GPSLatitudeRef
                const QString ref = tiff.string(valueOffset, count);
                if (!ref.isEmpty()) record.gpsLatitudeStr = ref;
                break;
            }
            case 0x0002: { // GPSLatitude (3 rationals: deg, min, sec)
                if (count >= 3 && tiff.canRead(valueOffset, 24)) {
                    const double deg = tiff.rational(valueOffset);
                    const double min = tiff.rational(valueOffset + 8);
                    const double sec = tiff.rational(valueOffset + 16);
                    double dec = deg + min / 60.0 + sec / 3600.0;
                    if (record.gpsLatitudeStr.startsWith(QLatin1Char('S'), Qt::CaseInsensitive)) dec = -dec;
                    record.latitude = dec;
                    record.hasGps = true;
                    record.gpsLatitudeStr = QString::asprintf("%d° %d' %.1f\" %s",
                        static_cast<int>(deg), static_cast<int>(min), sec,
                        record.latitude >= 0.0 ? "N" : "S");
                    record.items.append(MetadataItem{QStringLiteral("Exif.GPSInfo.GPSLatitude"), QStringLiteral("Latitude"), record.gpsLatitudeStr, QString::number(dec), QStringLiteral("GPS")});
                }
                break;
            }
            case 0x0003: { // GPSLongitudeRef
                const QString ref = tiff.string(valueOffset, count);
                if (!ref.isEmpty()) record.gpsLongitudeStr = ref;
                break;
            }
            case 0x0004: { // GPSLongitude (3 rationals: deg, min, sec)
                if (count >= 3 && tiff.canRead(valueOffset, 24)) {
                    const double deg = tiff.rational(valueOffset);
                    const double min = tiff.rational(valueOffset + 8);
                    const double sec = tiff.rational(valueOffset + 16);
                    double dec = deg + min / 60.0 + sec / 3600.0;
                    if (record.gpsLongitudeStr.startsWith(QLatin1Char('W'), Qt::CaseInsensitive)) dec = -dec;
                    record.longitude = dec;
                    record.hasGps = true;
                    record.gpsLongitudeStr = QString::asprintf("%d° %d' %.1f\" %s",
                        static_cast<int>(deg), static_cast<int>(min), sec,
                        record.longitude >= 0.0 ? "E" : "W");
                    record.items.append(MetadataItem{QStringLiteral("Exif.GPSInfo.GPSLongitude"), QStringLiteral("Longitude"), record.gpsLongitudeStr, QString::number(dec), QStringLiteral("GPS")});
                }
                break;
            }
            case 0x0006: { // GPSAltitude
                const double alt = tiff.rational(valueOffset);
                record.altitude = alt;
                record.hasGps = true;
                record.gpsAltitudeStr = QString::asprintf("%.1f m", alt);
                record.items.append(MetadataItem{QStringLiteral("Exif.GPSInfo.GPSAltitude"), QStringLiteral("Altitude"), record.gpsAltitudeStr, QString::number(alt), QStringLiteral("GPS")});
                break;
            }
            default:
                break;
            }
        }

        entryOffset += 12;
    }

    // Follow next IFD pointer if present (e.g. IFD1 thumbnail)
    if (tiff.canRead(entryOffset, 4)) {
        const quint32 nextIfd = tiff.u32(entryOffset);
        if (nextIfd > 0 && nextIfd < static_cast<quint32>(tiff.size)) {
            parseTiffIFD(tiff, static_cast<int>(nextIfd), record, visited, depth + 1, parentCategory);
        }
    }
}

void parseTiffPayload(const uchar* data, int size, MetadataRecord& record) {
    if (size < 8) return;
    TiffReader tiff;
    tiff.data = data;
    tiff.size = size;

    if (data[0] == 'I' && data[1] == 'I') {
        tiff.isLittleEndian = true;
    } else if (data[0] == 'M' && data[1] == 'M') {
        tiff.isLittleEndian = false;
    } else {
        return; // Not a valid TIFF header
    }

    const quint16 magic = tiff.u16(2);
    if (magic != 42 && magic != 0x002A) return;

    const quint32 firstIfd = tiff.u32(4);
    QSet<quint32> visited;
    parseTiffIFD(tiff, static_cast<int>(firstIfd), record, visited, 0);
}

void parseXmpPayload(const QByteArray& xmpData, MetadataRecord& record) {
    if (xmpData.isEmpty()) return;
    QXmlStreamReader xml(xmpData);

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString name = xml.name().toString();
            if (name == QStringLiteral("title") && record.iptcTitle.isEmpty()) {
                record.iptcTitle = xml.readElementText().trimmed();
                record.items.append(MetadataItem{QStringLiteral("XMP.dc.title"), QStringLiteral("Title"), record.iptcTitle, record.iptcTitle, QStringLiteral("IPTC")});
            } else if (name == QStringLiteral("creator") && record.iptcCreator.isEmpty()) {
                record.iptcCreator = xml.readElementText().trimmed();
                record.items.append(MetadataItem{QStringLiteral("XMP.dc.creator"), QStringLiteral("Creator"), record.iptcCreator, record.iptcCreator, QStringLiteral("IPTC")});
            } else if (name == QStringLiteral("description") && record.iptcDescription.isEmpty()) {
                record.iptcDescription = xml.readElementText().trimmed();
                record.items.append(MetadataItem{QStringLiteral("XMP.dc.description"), QStringLiteral("Description"), record.iptcDescription, record.iptcDescription, QStringLiteral("IPTC")});
            } else if (name == QStringLiteral("rights") && record.iptcCopyright.isEmpty()) {
                record.iptcCopyright = xml.readElementText().trimmed();
                record.items.append(MetadataItem{QStringLiteral("XMP.dc.rights"), QStringLiteral("Copyright"), record.iptcCopyright, record.iptcCopyright, QStringLiteral("IPTC")});
            }
        }
    }
}

void parseIptcPayload(const uchar* data, int size, MetadataRecord& record) {
    int i = 0;
    while (i + 5 <= size) {
        if (data[i] == 0x1C) { // IPTC tag marker
            const quint8 recordNum = data[i + 1];
            const quint8 datasetNum = data[i + 2];
            const quint16 dataLen = readU16BE(data + i + 3);
            i += 5;
            if (i + dataLen > size) break;

            if (recordNum == 2) { // Application Record
                const QString val = QString::fromUtf8(reinterpret_cast<const char*>(data + i), dataLen).trimmed();
                switch (datasetNum) {
                case 5: // Object Name / Title
                    if (record.iptcTitle.isEmpty()) record.iptcTitle = val;
                    record.items.append(MetadataItem{QStringLiteral("IPTC.Application2.ObjectName"), QStringLiteral("Title"), val, val, QStringLiteral("IPTC")});
                    break;
                case 25: // Keywords
                    record.iptcKeywords.append(val);
                    record.items.append(MetadataItem{QStringLiteral("IPTC.Application2.Keywords"), QStringLiteral("Keyword"), val, val, QStringLiteral("IPTC")});
                    break;
                case 80: // By-line
                    if (record.iptcCreator.isEmpty()) record.iptcCreator = val;
                    record.items.append(MetadataItem{QStringLiteral("IPTC.Application2.Byline"), QStringLiteral("Creator / Author"), val, val, QStringLiteral("IPTC")});
                    break;
                case 116: // Copyright Notice
                    if (record.iptcCopyright.isEmpty()) record.iptcCopyright = val;
                    record.items.append(MetadataItem{QStringLiteral("IPTC.Application2.CopyrightNotice"), QStringLiteral("Copyright"), val, val, QStringLiteral("IPTC")});
                    break;
                case 120: // Caption / Abstract
                    if (record.iptcDescription.isEmpty()) record.iptcDescription = val;
                    record.items.append(MetadataItem{QStringLiteral("IPTC.Application2.Caption"), QStringLiteral("Description"), val, val, QStringLiteral("IPTC")});
                    break;
                }
            }
            i += dataLen;
        } else {
            ++i;
        }
    }
}

} // namespace

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

    // One short-lived shared-delete handle serves the reader and the header read,
    // so parsing never blocks deleting or renaming the image away (EN-11).
    // An unreadable file keeps its name and size, as before; format and headers stay empty.
    QFile source;
    detail::openSharedRead(source, filePath);

    // Query basic metadata with QImageReader without decoding full pixels
    QImageReader reader(&source);
    const QByteArray format = reader.format();
    record.fileFormat = QString::fromLatin1(format).toUpper();

    const QSize size = reader.size();
    record.storedWidth = size.width();
    record.storedHeight = size.height();

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

    record.bitDepth = 8;
    record.colorSpace = QStringLiteral("sRGB");

    // Bounded binary read of headers through IoScheduler priority
    QByteArray headerBytes;
    if (source.isOpen() && source.seek(0)) {
        headerBytes = source.read(maxReadBytes);
    }
    source.close();

    const uchar* raw = reinterpret_cast<const uchar*>(headerBytes.constData());
    const int rawSize = headerBytes.size();

    if (rawSize >= 4) {
        // Format Dispatch
        if (raw[0] == 0xFF && raw[1] == 0xD8) {
            // --- JPEG ---
            int pos = 2;
            while (pos + 4 <= rawSize) {
                if (raw[pos] != 0xFF) {
                    ++pos;
                    continue;
                }
                const quint8 marker = raw[pos + 1];
                if (marker == 0xDA || marker == 0xD9) { // SOS (Start of Scan) or EOI
                    break;
                }
                const quint16 segmentLen = readU16BE(raw + pos + 2);
                if (segmentLen < 2 || pos + 2 + segmentLen > rawSize) {
                    break;
                }

                const uchar* payload = raw + pos + 4;
                const int payloadLen = segmentLen - 2;

                if (marker == 0xE1) { // APP1 (Exif or XMP)
                    if (payloadLen >= 6 && std::memcmp(payload, "Exif\0\0", 6) == 0) {
                        parseTiffPayload(payload + 6, payloadLen - 6, record);
                    } else if (payloadLen >= 29 && std::memcmp(payload, "http://ns.adobe.com/xap/1.0/\0", 29) == 0) {
                        const QByteArray xmp(reinterpret_cast<const char*>(payload + 29), payloadLen - 29);
                        parseXmpPayload(xmp, record);
                    }
                } else if (marker == 0xED) { // APP13 (Photoshop 3.0 / IPTC)
                    if (payloadLen >= 14 && std::memcmp(payload, "Photoshop 3.0\0", 14) == 0) {
                        parseIptcPayload(payload + 14, payloadLen - 14, record);
                    }
                } else if (marker == 0xE2) { // APP2 (ICC Profile)
                    if (payloadLen >= 12 && std::memcmp(payload, "ICC_PROFILE\0", 12) == 0) {
                        record.colorSpace = QStringLiteral("ICC Profile");
                    }
                }

                pos += 2 + segmentLen;
            }
        } else if (rawSize >= 8 && std::memcmp(raw, "\x89PNG\r\n\x1a\n", 8) == 0) {
            // --- PNG ---
            int pos = 8;
            while (pos + 8 <= rawSize) {
                const quint32 chunkLen = readU32BE(raw + pos);
                const char* chunkType = reinterpret_cast<const char*>(raw + pos + 4);
                pos += 8;
                if (pos + chunkLen > static_cast<quint32>(rawSize)) break;

                if (std::memcmp(chunkType, "eXIf", 4) == 0) {
                    parseTiffPayload(raw + pos, chunkLen, record);
                } else if (std::memcmp(chunkType, "tEXt", 4) == 0 || std::memcmp(chunkType, "iTXt", 4) == 0) {
                    // Keyword / null / text
                    const char* textPtr = reinterpret_cast<const char*>(raw + pos);
                    int keyLen = 0;
                    while (keyLen < static_cast<int>(chunkLen) && textPtr[keyLen] != '\0') ++keyLen;
                    if (keyLen < static_cast<int>(chunkLen)) {
                        const QString key = QString::fromUtf8(textPtr, keyLen);
                        const QString val = QString::fromUtf8(textPtr + keyLen + 1, chunkLen - keyLen - 1).trimmed();
                        if (key.compare(QStringLiteral("Title"), Qt::CaseInsensitive) == 0) record.iptcTitle = val;
                        else if (key.compare(QStringLiteral("Author"), Qt::CaseInsensitive) == 0) record.iptcCreator = val;
                        else if (key.compare(QStringLiteral("Description"), Qt::CaseInsensitive) == 0) record.iptcDescription = val;
                        else if (key.compare(QStringLiteral("Copyright"), Qt::CaseInsensitive) == 0) record.iptcCopyright = val;
                        record.items.append(MetadataItem{QStringLiteral("PNG.Text.") + key, key, val, val, QStringLiteral("IPTC")});
                    }
                }

                pos += chunkLen + 4; // Skip CRC
            }
        } else if (rawSize >= 12 && std::memcmp(raw, "RIFF", 4) == 0 && std::memcmp(raw + 8, "WEBP", 4) == 0) {
            // --- WebP ---
            int pos = 12;
            while (pos + 8 <= rawSize) {
                const char* chunkType = reinterpret_cast<const char*>(raw + pos);
                const quint32 chunkLen = readU32LE(raw + pos + 4);
                pos += 8;
                if (pos + chunkLen > static_cast<quint32>(rawSize)) break;

                if (std::memcmp(chunkType, "EXIF", 4) == 0) {
                    const uchar* exifData = raw + pos;
                    int exifLen = static_cast<int>(chunkLen);
                    if (exifLen >= 6 && std::memcmp(exifData, "Exif\0\0", 6) == 0) {
                        exifData += 6;
                        exifLen -= 6;
                    }
                    parseTiffPayload(exifData, exifLen, record);
                } else if (std::memcmp(chunkType, "XMP ", 4) == 0) {
                    const QByteArray xmp(reinterpret_cast<const char*>(raw + pos), chunkLen);
                    parseXmpPayload(xmp, record);
                }

                pos += chunkLen + (chunkLen % 2); // WebP chunks are padded to 2-byte boundary
            }
        } else if ((raw[0] == 'I' && raw[1] == 'I') || (raw[0] == 'M' && raw[1] == 'M')) {
            // --- TIFF ---
            parseTiffPayload(raw, rawSize, record);
        }
    }

    // Append primary summary tags if not already present
    record.items.prepend(MetadataItem{
        QStringLiteral("File.Size"),
        QStringLiteral("File Size"),
        QString::asprintf("%.1f KB (%lld bytes)", record.fileSize / 1024.0, record.fileSize),
        QString::number(record.fileSize),
        QStringLiteral("Summary")
    });

    if (record.displayWidth > 0 && record.displayHeight > 0) {
        const double mp = (record.displayWidth * static_cast<double>(record.displayHeight)) / 1000000.0;
        record.items.prepend(MetadataItem{
            QStringLiteral("Image.Dimensions"),
            QStringLiteral("Dimensions"),
            QString::asprintf("%d × %d (%.1f MP)", record.displayWidth, record.displayHeight, mp),
            QString::asprintf("%dx%d", record.storedWidth, record.storedHeight),
            QStringLiteral("Image")
        });
    }

    record.items.prepend(MetadataItem{
        QStringLiteral("File.Name"),
        QStringLiteral("File Name"),
        record.fileName,
        record.fileName,
        QStringLiteral("Summary")
    });

    record.hasMetadata = record.items.size() > 2 || record.hasGps || !record.cameraModel.isEmpty();
    return record;
}

} // namespace glancelot
