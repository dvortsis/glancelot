#pragma once
#include <atomic>
#include <cstdint>

// A canvas can briefly have renderer incarnations on two scene-graph threads
// while moving between windows. Both share this admission counter.
class CanvasTextureAdmission final {
    std::atomic<std::int64_t> bytes_{};
public:
    bool reserve(std::int64_t bytes,std::int64_t limit) {
        if(bytes<0||bytes>limit)return false;
        auto live=bytes_.load(std::memory_order_relaxed);
        do {if(live>limit-bytes)return false;}
        while(!bytes_.compare_exchange_weak(live,live+bytes,std::memory_order_acq_rel,std::memory_order_relaxed));
        return true;
    }
    void release(std::int64_t bytes){bytes_.fetch_sub(bytes,std::memory_order_release);}
    std::int64_t bytes() const{return bytes_.load(std::memory_order_acquire);}
};
