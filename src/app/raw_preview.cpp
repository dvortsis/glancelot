#include "glancelot/raw_preview.hpp"
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <algorithm>
#include <vector>

namespace glancelot {

namespace {

inline quint16 readU16LE(const uchar* p) { return static_cast<quint16>(p[0] | (p[1] << 8)); }
inline quint16 readU16BE(const uchar* p) { return static_cast<quint16>((p[0] << 8) | p[1]); }
inline quint32 readU32LE(const uchar* p) { return static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
inline quint32 readU32BE(const uchar* p) { return static_cast<quint32>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

struct TiffReader {
    const uchar* data{nullptr};
    qint64 size{0};
    bool isLittleEndian{true};

    bool canRead(qint64 offset, qint64 len) const {
        return offset >= 0 && len >= 0 && (offset + len) <= size;
    }

    quint16 u16(qint64 offset) const {
        if (!canRead(offset, 2)) return 0;
        return isLittleEndian ? readU16LE(data + offset) : readU16BE(data + offset);
    }

    quint32 u32(qint64 offset) const {
        if (!canRead(offset, 4)) return 0;
        return isLittleEndian ? readU32LE(data + offset) : readU32BE(data + offset);
    }
};

bool parseJpegDimensions(const uchar* data, qint64 size, int& width, int& height) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    qint64 pos = 2;
    while (pos + 4 < size) {
        if (data[pos] != 0xFF) {
            ++pos;
            continue;
        }
        const uchar marker = data[pos + 1];
        if (marker == 0xD9 || marker == 0xDA) break; // EOI or Start of Scan
        if (marker == 0xFF || marker == 0x00) {
            pos += 2;
            continue;
        }

        const quint16 length = readU16BE(data + pos + 2);
        if (length < 2 || pos + 2 + length > size) break;

        // SOF0 (baseline), SOF1 (extended), SOF2 (progressive)
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (length >= 7) {
                height = readU16BE(data + pos + 5);
                width = readU16BE(data + pos + 7);
                return width > 0 && height > 0;
            }
        }
        pos += 2 + length;
    }
    return false;
}

struct PreviewCandidate {
    qint64 offset{0};
    qint64 length{0};
    int width{0};
    int height{0};
};

void checkCandidate(const uchar* data, qint64 size, qint64 offset, qint64 length, std::vector<PreviewCandidate>& candidates) {
    if (offset < 0 || length < 1024 || offset + length > size) return;
    if (data[offset] != 0xFF || data[offset + 1] != 0xD8) return;

    int w = 0, h = 0;
    parseJpegDimensions(data + offset, length, w, h);
    candidates.push_back({offset, length, w, h});
}

void parseIfdPreviews(const TiffReader& tiff, qint64 ifdOffset, std::vector<PreviewCandidate>& candidates, int depth = 0) {
    if (depth > 10 || ifdOffset <= 0 || !tiff.canRead(ifdOffset, 2)) return;

    const quint16 numEntries = tiff.u16(ifdOffset);
    qint64 entryOffset = ifdOffset + 2;

    qint64 jpegOffset = 0;
    qint64 jpegLen = 0;
    std::vector<qint64> subIfds;

    for (int i = 0; i < numEntries; ++i) {
        if (!tiff.canRead(entryOffset, 12)) break;
        const quint16 tag = tiff.u16(entryOffset);
        const quint16 type = tiff.u16(entryOffset + 2);
        const quint32 count = tiff.u32(entryOffset + 4);

        qint64 valOffset = entryOffset + 8;
        if (count > 1 || type == 3 || type == 4) {
            valOffset = tiff.isLittleEndian ? readU32LE(tiff.data + entryOffset + 8) : readU32BE(tiff.data + entryOffset + 8);
        }

        if (tag == 0x0201) { // JPEGInterchangeFormat
            jpegOffset = valOffset;
        } else if (tag == 0x0202) { // JPEGInterchangeFormatLength
            jpegLen = valOffset;
        } else if (tag == 0x0111) { // StripOffsets
            if (jpegOffset == 0) jpegOffset = valOffset;
        } else if (tag == 0x0117) { // StripByteCounts
            if (jpegLen == 0) jpegLen = valOffset;
        } else if (tag == 0x014A) { // SubIFDs
            if (count > 0 && count < 20) {
                for (quint32 s = 0; s < count; ++s) {
                    if (tiff.canRead(valOffset + s * 4, 4)) {
                        subIfds.push_back(tiff.u32(valOffset + s * 4));
                    }
                }
            }
        }

        entryOffset += 12;
    }

    if (jpegOffset > 0 && jpegLen > 0) {
        checkCandidate(tiff.data, tiff.size, jpegOffset, jpegLen, candidates);
    }

    // Traverse subIFDs
    for (qint64 sub : subIfds) {
        parseIfdPreviews(tiff, sub, candidates, depth + 1);
    }

    // Check next IFD
    if (tiff.canRead(entryOffset, 4)) {
        const quint32 nextIfd = tiff.u32(entryOffset);
        if (nextIfd > 0 && nextIfd < static_cast<quint32>(tiff.size)) {
            parseIfdPreviews(tiff, nextIfd, candidates, depth + 1);
        }
    }
}

void scanForJpegs(const uchar* data, qint64 size, std::vector<PreviewCandidate>& candidates) {
    // Scan for JPEG SOI \xFF\xD8\xFF
    qint64 pos = 0;
    while (pos + 1024 < size) {
        if (data[pos] == 0xFF && data[pos + 1] == 0xD8 && data[pos + 2] == 0xFF) {
            // Find EOI \xFF\xD9
            qint64 eoi = pos + 1024;
            const qint64 maxScan = std::min(size, pos + 32 * 1024 * 1024); // max 32MB per preview
            while (eoi + 1 < maxScan) {
                if (data[eoi] == 0xFF && data[eoi + 1] == 0xD9) {
                    const qint64 len = (eoi + 2) - pos;
                    int w = 0, h = 0;
                    if (parseJpegDimensions(data + pos, len, w, h) && w >= 64 && h >= 64) {
                        candidates.push_back({pos, len, w, h});
                        pos = eoi + 2;
                        break;
                    }
                }
                ++eoi;
            }
        }
        pos += 2;
    }
}

} // namespace

bool RawPreviewExtractor::isRawExtension(const QString& pathOrExtension) {
    QString ext = pathOrExtension;
    const int dot = ext.lastIndexOf(QLatin1Char('.'));
    if (dot != -1) {
        ext = ext.mid(dot);
    }
    ext = ext.toLower();
    if (!ext.startsWith(QLatin1Char('.'))) {
        ext.prepend(QLatin1Char('.'));
    }

    static const QStringList rawExts = {
        QStringLiteral(".dng"), QStringLiteral(".cr2"), QStringLiteral(".cr3"),
        QStringLiteral(".nef"), QStringLiteral(".nrw"), QStringLiteral(".arw"),
        QStringLiteral(".sr2"), QStringLiteral(".raf"), QStringLiteral(".orf"),
        QStringLiteral(".rw2"), QStringLiteral(".pef"), QStringLiteral(".iiq")
    };
    return rawExts.contains(ext);
}

RawPreviewResult RawPreviewExtractor::extractPreview(const QString& filePath) {
    RawPreviewResult result;
    QElapsedTimer timer;
    timer.start();

    QFileInfo info(filePath);
    if (!info.exists()) {
        result.error = QStringLiteral("File does not exist: %1").arg(filePath);
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot open file: %1").arg(filePath);
        return result;
    }

    // Map file if possible, or read into buffer (up to 256MB)
    const qint64 fileSize = file.size();
    if (fileSize < 16) {
        result.error = QStringLiteral("RAW file is too small or truncated.");
        return result;
    }

    uchar* mapped = file.map(0, fileSize);
    if (mapped) {
        result = extractPreviewFromData(mapped, fileSize);
        file.unmap(mapped);
    } else {
        const qint64 readLimit = std::min(fileSize, static_cast<qint64>(128 * 1024 * 1024));
        const QByteArray data = file.read(readLimit);
        result = extractPreviewFromData(reinterpret_cast<const uchar*>(data.constData()), data.size());
    }

    result.extractionTimeUs = timer.nsecsElapsed() / 1000;
    return result;
}

RawPreviewResult RawPreviewExtractor::extractPreviewFromData(const uchar* data, qint64 size) {
    RawPreviewResult result;
    if (!data || size < 16) {
        result.error = QStringLiteral("Input buffer is empty or too small.");
        return result;
    }

    std::vector<PreviewCandidate> candidates;

    // 1. TIFF IFD parsing
    bool isTiff = false;
    bool isLE = true;
    if (size >= 8) {
        if (data[0] == 'I' && data[1] == 'I' && ((data[2] == 0x2A && data[3] == 0x00) || (data[2] == 0x55 && data[3] == 0x00))) {
            isTiff = true;
            isLE = true;
        } else if (data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A) {
            isTiff = true;
            isLE = false;
        }
    }

    if (isTiff) {
        TiffReader tiff;
        tiff.data = data;
        tiff.size = size;
        tiff.isLittleEndian = isLE;

        const quint32 firstIfd = tiff.u32(4);
        if (firstIfd > 0 && firstIfd < static_cast<quint32>(size)) {
            parseIfdPreviews(tiff, firstIfd, candidates);
        }
    }

    // 2. If no candidate found via IFD (or for non-TIFF RAWs like CR3 / RAF), scan for embedded JPEGs
    if (candidates.empty()) {
        scanForJpegs(data, size, candidates);
    }

    if (candidates.empty()) {
        result.error = QStringLiteral("No embedded preview found in RAW file.");
        return result;
    }

    // 3. Select the best preview: largest pixel area (width * height), or largest byte size
    const auto bestIt = std::max_element(candidates.begin(), candidates.end(), [](const PreviewCandidate& a, const PreviewCandidate& b) {
        const qint64 areaA = static_cast<qint64>(a.width) * a.height;
        const qint64 areaB = static_cast<qint64>(b.width) * b.height;
        if (areaA != areaB) return areaA < areaB;
        return a.length < b.length;
    });

    const PreviewCandidate& best = *bestIt;
    if (best.offset + best.length > size || best.length <= 0) {
        result.error = QStringLiteral("Invalid embedded preview offset or bounds.");
        return result;
    }

    result.jpegData = QByteArray(reinterpret_cast<const char*>(data + best.offset), static_cast<int>(best.length));
    result.width = best.width;
    result.height = best.height;
    result.success = !result.jpegData.isEmpty();
    if (!result.success) {
        result.error = QStringLiteral("Extracted preview byte data is empty.");
    }
    return result;
}

} // namespace glancelot

