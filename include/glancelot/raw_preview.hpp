#pragma once
#include <QString>
#include <QByteArray>
#include <QSize>

namespace glancelot {

struct RawPreviewResult {
    bool success{false};
    QByteArray jpegData;
    QString error;
    int width{0};
    int height{0};
    qint64 extractionTimeUs{0};
};

class RawPreviewExtractor {
public:
    static bool isRawExtension(const QString& pathOrExtension);
    static RawPreviewResult extractPreview(const QString& filePath);
    static RawPreviewResult extractPreviewFromData(const uchar* data, qint64 size);
};

} // namespace glancelot

