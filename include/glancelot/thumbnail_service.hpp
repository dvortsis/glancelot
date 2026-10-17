#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <memory>

// Bounded, cancellable thumbnail generation. Decoding happens only in the glancelot-decoder
// --thumbnail-server worker processes; this class owns scheduling, deadlines and a size-capped memory cache.
// Lives on the GUI thread. Results are delivered by queued signal, never synchronously from request().
// Contract (frozen by the lead; change only through the lead):
//  * request() returns a non-zero id, or 0 when the bounded queue is full or the epoch is stale (no signal for 0).
//  * Exactly one of ready()/failed() is emitted per accepted id unless cancel(id) or setEpoch() discarded it.
//  * A result whose epoch is older than the current epoch is dropped and never emitted.
//  * Thumbnails are never written to disk; the cache is memory only.
class ThumbnailService : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEdge = 256;
    static constexpr int kMinEdge = 16;
    static constexpr int kMaxQueue = 256;
    static constexpr int kMaxWorkers = 2;
    static constexpr qint64 kCacheBytes = 64LL * 1024 * 1024;

    explicit ThumbnailService(const QString& workerProgram, QObject* parent = nullptr);
    ~ThumbnailService() override;

    quint64 request(const QString& path, int edge, quint64 epoch);
    void cancel(quint64 id);
    // Advances the content epoch: clears the cache, drops queued work and discards in-flight results.
    void setEpoch(quint64 epoch);
    quint64 epoch() const;
    void setWorkerCount(int count);
    void setDeadlineMs(int milliseconds);
    int pending() const;
    int active() const;
    qint64 cacheBytes() const;
    // Stops every worker and waits for exit. Required before destroying temporary media in tests.
    void shutdown();
signals:
    void ready(quint64 id, const QImage& image);
    void failed(quint64 id, const QString& message);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
