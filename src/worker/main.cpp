#include <QCoreApplication>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedMemory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
void send(const QJsonObject& value) {
    const auto line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}
int fail(const QString& message) { send({{"phase", "error"}, {"error", message}}); return 1; }
bool openShared(QFile& file, const QString& path) {
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (descriptor == -1) { CloseHandle(handle); return false; }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { _close(descriptor); return false; }
    return true;
#else
    file.setFileName(path);
    return file.open(QIODevice::ReadOnly);
#endif
}

// Thumbnail server protocol. Requests: one JSON line {"id":N,"path":"...","edge":E} on stdin.
// Replies on stdout: {"id":N,"phase":"error","error":"..."} or {"id":N,"phase":"thumb","width":W,"height":H,"bytes":B}
// followed by exactly B bytes of tightly packed RGBA8888 (B == W*H*4, W,H <= edge <= 256).
constexpr int kMaxEdge = 256, kMinEdge = 16;
constexpr size_t kMaxRequestBytes = 131072;

bool readRequestLine(QByteArray& line) {
    line.clear();
    char chunk[4096];
    while (std::fgets(chunk, sizeof(chunk), stdin)) {
        line += chunk;
        if (line.endsWith('\n')) return true;
        if (size_t(line.size()) > kMaxRequestBytes) return false;
    }
    return false;
}

void sendThumbnailError(qint64 id, const QString& message) {
    send({{"id", double(id)}, {"phase", "error"}, {"error", message}});
}

QImage makeThumbnail(const QString& path, int edge, QString& error) {
    // QFileInfo stats lazily: read the values now, or the later comparison would compare two post-decode stats.
    const QFileInfo initialInfo(path);
    const qint64 initialSize = initialInfo.size();
    const QDateTime initialModified = initialInfo.lastModified();
    QFile source;
    if (!openShared(source, path)) { error = "Cannot read this file."; return {}; }
    QImageReader reader(&source);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format != "jpeg" && format != "jpg" && format != "png") { error = "Unsupported format for thumbnails."; return {}; }
    // Scale in the stored orientation; the EXIF transform is applied after decoding, so the final
    // aspect ratio is correct even when the orientation swaps the axes.
    const QSize stored = reader.size();
    if (!stored.isValid() || stored.width() > 32768 || stored.height() > 32768) { error = "Invalid or unsupported image dimensions."; return {}; }
    const double factor = (std::min)(1.0, double(edge) / (std::max)(stored.width(), stored.height()));
    if (factor < 1.0)
        reader.setScaledSize(QSize((std::max)(1, int(std::lround(stored.width() * factor))), (std::max)(1, int(std::lround(stored.height() * factor)))));
    QImage image = reader.read();
    source.close();
    if (image.isNull()) { error = "Image decode failed: " + reader.errorString(); return {}; }
    const QFileInfo after(path);
    if (!after.exists() || after.size() != initialSize || after.lastModified() != initialModified) { error = "File changed while decoding."; return {}; }
    if ((std::max)(image.width(), image.height()) > edge)
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888);
    if (image.isNull() || image.format() != QImage::Format_RGBA8888 || image.width() > edge || image.height() > edge) {
        error = "Cannot convert thumbnail pixels."; return {};
    }
    return image;
}

int thumbnailServer() {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QImageReader::setAllocationLimit(256);
    QByteArray line;
    while (readRequestLine(line)) {
        QJsonParseError parse;
        const auto request = QJsonDocument::fromJson(line, &parse).object();
        const qint64 id = request.value("id").toInteger(-1);
        if (parse.error != QJsonParseError::NoError || id < 0) return 2;
        const QString path = request.value("path").toString();
        const int edge = request.value("edge").toInt();
        if (path.isEmpty() || edge < kMinEdge || edge > kMaxEdge) { sendThumbnailError(id, "Invalid thumbnail request."); continue; }
        QString error;
        const QImage image = makeThumbnail(path, edge, error);
        if (image.isNull()) { sendThumbnailError(id, error); continue; }
        const qint64 bytes = qint64(image.width()) * image.height() * 4;
        send({{"id", double(id)}, {"phase", "thumb"}, {"width", image.width()}, {"height", image.height()}, {"bytes", double(bytes)}});
        for (int y = 0; y < image.height(); ++y) std::fwrite(image.constScanLine(y), 1, size_t(image.width()) * 4, stdout);
        std::fflush(stdout);
    }
    return 0;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 2 && app.arguments().at(1) == "--thumbnail-server") return thumbnailServer();
    if (app.arguments().size() != 2) return fail("Expected one image path.");
    const QString path = app.arguments().at(1);
    const QFileInfo initialInfo(path);
    const qint64 initialSize = initialInfo.size();
    const QDateTime initialModified = initialInfo.lastModified();
    QFile source;
    if (!openShared(source, path)) return fail("Cannot read this file. Check that it exists and is accessible.");
    QImageReader::setAllocationLimit(256);
    QImageReader reader(&source);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format != "jpeg" && format != "jpg" && format != "png") return fail("This alpha supports JPEG and PNG images.");
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 32768 || size.height() > 32768) return fail("Invalid or unsupported image dimensions.");
    const qint64 capacity = qint64(size.width()) * size.height() * 4;
    if (capacity <= 0 || capacity > 256LL*1024*1024) return fail("Image exceeds the 256 MiB decoded-image limit.");
    send({{"phase", "probe"}, {"width", size.width()}, {"height", size.height()}, {"bytes", double(capacity)}});
    char request[4096]{};
    if (!std::fgets(request, sizeof(request), stdin)) return fail("Decode was cancelled before memory admission.");
    QJsonParseError parseError;
    const auto configuration = QJsonDocument::fromJson(QByteArray(request), &parseError).object();
    if (parseError.error != QJsonParseError::NoError || configuration.value("key").toString().isEmpty()) return fail("Invalid shared-memory admission request.");
    QSharedMemory transfer(configuration.value("key").toString());
    if (!transfer.attach(QSharedMemory::ReadWrite) || transfer.size() < capacity) return fail("Cannot attach admitted pixel transfer buffer.");
    QImage image = reader.read();
    source.close();
    if (image.isNull()) return fail("Image decode failed: " + reader.errorString());
    const QFileInfo after(path);
    if (!after.exists() || after.size() != initialSize || after.lastModified() != initialModified) return fail("File changed while decoding; retry when the writer finishes.");
    const bool tagged = image.colorSpace().isValid();
    if (!tagged) image.setColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888);
    if (image.isNull() || image.format() != QImage::Format_RGBA8888) return fail("Cannot convert source pixels to the SDR working format.");
    const qint64 bytes = qint64(image.width()) * image.height() * 4;
    if (bytes > transfer.size() || bytes != capacity) return fail("Decoded dimensions exceed the admitted transfer.");
    if (!transfer.lock()) return fail("Cannot lock pixel transfer buffer.");
    auto* output = static_cast<unsigned char*>(transfer.data());
    for (int y=0; y<image.height(); ++y) std::memcpy(output+qint64(y)*image.width()*4, image.constScanLine(y), size_t(image.width())*4);
    transfer.unlock();
    send({{"phase", "decoded"}, {"width", image.width()}, {"height", image.height()}, {"stride", image.width()*4},
          {"color", tagged ? "Embedded profile converted to sRGB" : "Untagged source assumed sRGB"}});
    // Parent copies the immutable pixels before acknowledging. No original file handle is held here.
    if (!std::fgets(request, sizeof(request), stdin)) return 1;
    return 0;
}
