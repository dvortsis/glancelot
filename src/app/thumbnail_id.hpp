#pragma once
#include <QByteArray>
#include <QString>
#include <optional>

// image://thumb/<epoch>/<edge>/<base64url(utf-8 path)>. Base64url keeps the id free of separators and
// immune to URL percent-decoding differences between Qt versions.
namespace thumbnail_id {
struct Parts { quint64 epoch{}; int edge{}; QString path; };

inline QString encode(quint64 epoch, int edge, const QString& path) {
    return QString::number(epoch) + '/' + QString::number(edge) + '/'
        + QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline std::optional<Parts> decode(const QString& id) {
    const auto pieces = id.split('/');
    if (pieces.size() != 3) return std::nullopt;
    bool epochOk = false, edgeOk = false;
    Parts parts;
    parts.epoch = pieces.at(0).toULongLong(&epochOk);
    parts.edge = pieces.at(1).toInt(&edgeOk);
    const auto bytes = QByteArray::fromBase64Encoding(pieces.at(2).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals | QByteArray::AbortOnBase64DecodingErrors);
    if (!epochOk || !edgeOk || !bytes) return std::nullopt;
    parts.path = QString::fromUtf8(*bytes);
    return parts;
}
}
