#pragma once
#include "glancelot/resource_ledger.hpp"
#include <QString>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// A canvas can briefly have renderer incarnations on two scene-graph threads
// while moving between windows. Both share this admission counter.
//
// E1 (RESOURCE-LEDGER M1): one GpuTextures owner per canvas group, whose owner quota is the group's aggregate texture
// budget (the `limit` each call passes). Every admitted texture holds a Reservation, so the ledger sees the same bytes
// this counter does; the call sites, the bounded retry and the arithmetic are unchanged. The local check runs first
// and the ledger second, so with Enforcement::Shadow nothing is decided differently.
class CanvasTextureAdmission final {
    std::mutex mutex_;   // render threads only; never held while a renderer runs user code
    std::vector<glancelot::resource::Reservation> held_;
    glancelot::resource::OwnerId owner_;
    bool ownerTried_{};
    std::atomic<std::int64_t> bytes_{};
    std::atomic<bool> retry_{};
public:
    CanvasTextureAdmission() = default;
    CanvasTextureAdmission(const CanvasTextureAdmission&) = delete;
    CanvasTextureAdmission& operator=(const CanvasTextureAdmission&) = delete;
    ~CanvasTextureAdmission() {
        std::lock_guard lock(mutex_);
        held_.clear();   // releases every reservation exactly once
        if (owner_) glancelot::resource::ResourceLedger::instance().unregisterOwner(owner_);
    }
    bool beginRetry(){return !retry_.exchange(true,std::memory_order_acq_rel);}
    void endRetry(){retry_.store(false,std::memory_order_release);}
    bool reserve(std::int64_t bytes,std::int64_t limit) {
        if(bytes<0||bytes>limit)return false;
        std::lock_guard lock(mutex_);
        const auto live=bytes_.load(std::memory_order_relaxed);
        if(live>limit-bytes)return false;
        auto& ledger=glancelot::resource::ResourceLedger::instance();
        if(!std::exchange(ownerTried_,true))owner_=ledger.registerOwner(QStringLiteral("canvas-group"));
        if(owner_) {
            // No owner (kMaxOwners reached) keeps today's local admission rather than refusing every texture.
            ledger.setOwnerQuota(owner_,glancelot::resource::Ledger::GpuTextures,limit);
            auto reservation=ledger.tryReserve({glancelot::resource::Ledger::GpuTextures,glancelot::resource::Priority::Visible,
                                                owner_,bytes,0,0,"canvas-texture"});
            if(!reservation)return false;
            held_.push_back(std::move(reservation));
        }
        bytes_.fetch_add(bytes,std::memory_order_acq_rel);
        return true;
    }
    void release(std::int64_t bytes) {
        std::lock_guard lock(mutex_);
        for(auto it=held_.begin();it!=held_.end();++it) {
            if(it->bytes()!=bytes)continue;
            it->release();held_.erase(it);break;
        }
        bytes_.fetch_sub(bytes,std::memory_order_release);
    }
    std::int64_t bytes() const{return bytes_.load(std::memory_order_acquire);}
    glancelot::resource::OwnerId ledgerOwner() const{return owner_;}
};
