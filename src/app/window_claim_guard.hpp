#pragma once
#include "glancelot/settings_recovery.hpp"
#include "file_lock.hpp"
#include <atomic>

namespace glancelot::detail {
struct WindowClaimState {
    QString directory,namespaceId,nonce;
    int slot{};
    qint64 pid{},stamp{};
    std::shared_ptr<const void> lifetime;
    std::atomic_bool active{true};
};
struct WindowClaimAccess {
    static std::shared_ptr<const WindowClaimState> get(const WindowClaim& claim){return claim.state_;}
};
}
namespace glancelot::recovery::ownership {
struct Guard {
    Status status;
    QString directory,namespaceId;
    int slot{};
    std::shared_ptr<const glancelot::detail::WindowClaimState> claim;
    std::shared_ptr<const void> lifetime;
    std::unique_ptr<FileLock> sessionsLock;
    bool held() const {return status.code==Code::Ok&&bool(sessionsLock);}
};
// Acquire sessions FIRST and keep this guard through the settings transaction.
// Offline checks do not claim a slot, reset counters, prune records or scan media.
Guard acquire(const WindowClaim&,const Context&,Options = {});
Guard acquireOffline(int slot,const Context&,Options = {});
}
