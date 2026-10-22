#pragma once
#include <memory>

class WindowSlot;
namespace glancelot {
namespace detail { struct WindowClaimState; struct WindowClaimAccess; }

// SAFE-MODE-01 lead-frozen value capability. Only WindowSlot can capture one.
// A captured identity is not proof of current ownership: every recovery commit
// revalidates namespace/slot/pid/start/nonce under the sessions lock and keeps
// that guard through settings commit. No references to a WindowSlot/QObject.
class WindowClaim {
public:
    WindowClaim() = default;
    bool hasIdentity() const noexcept { return bool(state_); }
private:
    friend class ::WindowSlot;
    friend struct detail::WindowClaimAccess;
    std::shared_ptr<const detail::WindowClaimState> state_;
};
}
