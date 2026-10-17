#include "thumbnail_provider.hpp"
#include "thumbnail_id.hpp"
#include "glancelot/thumbnail_service.hpp"
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QThread>
#include <atomic>

namespace {
// Qt may construct responses on its pixmap-reader thread. The response moves itself to the service's
// (GUI) thread before touching the service; cancel() may arrive from either thread.
class Response final : public QQuickImageResponse {
public:
    Response(ThumbnailService* service, quint64 epoch, int edge, const QString& path)
        : service_(service), epoch_(epoch), edge_(edge), path_(path) {}
    void attach() {
        moveToThread(service_->thread());
        QMetaObject::invokeMethod(this, &Response::start, Qt::QueuedConnection);
    }
    QQuickTextureFactory* textureFactory() const override { return image_.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return error_; }
    void cancel() override {
        cancelled_ = true;
        QMetaObject::invokeMethod(this, &Response::cancelOnOwner, Qt::QueuedConnection);
    }
private:
    void start() {
        if (cancelled_ || done_) { finishNow(); return; }
        if (path_.isEmpty()) { error_ = "Invalid thumbnail request."; finishNow(); return; }
        id_ = service_->request(path_, edge_, epoch_);
        if (id_ == 0) { error_ = "Thumbnail queue is full or out of date."; finishNow(); return; }
        connect(service_, &ThumbnailService::ready, this, [this](quint64 id, const QImage& image) {
            if (id != id_) return;
            image_ = image; finishNow();
        });
        connect(service_, &ThumbnailService::failed, this, [this](quint64 id, const QString& message) {
            if (id != id_) return;
            error_ = message; finishNow();
        });
    }
    void cancelOnOwner() {
        if (done_) return;
        if (id_ != 0) service_->cancel(id_);
        finishNow();
    }
    void finishNow() { if (done_) return; done_ = true; emit finished(); }
    ThumbnailService* service_;
    quint64 epoch_{}, id_{};
    int edge_{};
    QString path_;
    QImage image_;
    QString error_;
    std::atomic<bool> cancelled_{false};
    bool done_{};
};
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize&) {
    const auto parts = thumbnail_id::decode(id);
    auto* response = parts ? new Response(service_, parts->epoch, parts->edge, parts->path) : new Response(service_, 0, 0, {});
    response->attach();
    return response;
}
