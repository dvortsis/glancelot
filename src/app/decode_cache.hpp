#pragma once
#include <QCache>
#include <QImage>
#include <QString>

namespace glancelot::detail {
// EN-05: the controller's decoded-image cache. Entries are verified immutable
// RGBA pixels keyed by fs_watch::key and bound to the worker's source token;
// pixels are reused only when a fresh revision probe returns the same token.
// GUI-thread only; it never touches the filesystem.
class DecodeCache {
public:
    static constexpr int capacityKiB=128*1024;
    static int costKiB(const QImage& pixels) { return int((pixels.sizeInBytes()+1023)/1024); }
    bool contains(const QString& key) const { return cache.contains(key); }
    // Pixels for key when sourceToken is non-empty and matches the stored token, else a null image.
    QImage reuse(const QString& key,const QString& sourceToken) const;
    // Stores pixels unless the token is empty or the image exceeds capacity; returns whether it stored.
    bool insert(const QString& key,QImage pixels,const QString& sourceToken);
    bool remove(const QString& key) { return cache.remove(key); }
    void clear() { cache.clear(); }
    qint64 bytes() const { return qint64(cache.totalCost())*1024; }
    int count() const { return int(cache.count()); }
private:
    struct CachedImage { QImage pixels; QString sourceToken; };
    QCache<QString,CachedImage> cache{capacityKiB}; // KiB of verified immutable RGBA pixels.
};
}
