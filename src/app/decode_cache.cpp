#include "decode_cache.hpp"

namespace glancelot::detail {
QImage DecodeCache::reuse(const QString& key,const QString& sourceToken) const {
    const auto* cached=cache.object(key);
    return cached&&!sourceToken.isEmpty()&&cached->sourceToken==sourceToken?cached->pixels:QImage{};
}
bool DecodeCache::insert(const QString& key,QImage pixels,const QString& sourceToken) {
    const int cost=costKiB(pixels);
    if(sourceToken.isEmpty()||cost>cache.maxCost())return false;
    return cache.insert(key,new CachedImage{std::move(pixels),sourceToken},cost);
}
}
