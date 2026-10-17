#pragma once
#include <QQuickAsyncImageProvider>

class ThumbnailService;

// image://thumb/<epoch>/<edge>/<base64url path> (see thumbnail_id.hpp). Requests are served by ThumbnailService; a QML Image
// that is destroyed or retargeted cancels its response, which removes queued work for scrolled-away items.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    explicit ThumbnailProvider(ThumbnailService* service) : service_(service) {}
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
private:
    ThumbnailService* service_;
};
