#pragma once
#include <QString>
#include <QtGlobal>
#include "window_claim.hpp"

// Stable opening-order window slots, coordinated across independent viewer processes.
// Contract (frozen by the lead; change only through the lead):
//  * The first live window of a session is slot 1, the next 2, and so on. A slot number is never reused while
//    any window of the session is alive; closing windows in any order never renumbers the others, and a window
//    opened later takes the next number after the highest one handed out this session.
//  * When no window is alive (all closed or crashed) the next claim starts again at 1.
//  * Coordination state lives in sessions.json beside settings.json under a cross-process lock; entries of dead
//    processes (checked by pid plus process start time, so pid reuse cannot keep a slot alive) are pruned.
//  * number() == 0 means coordination was unavailable (lock contended past its timeout, unwritable folder, or a
//    sessions.json that is unreadable/damaged/oversized/from another schema); the window runs without position
//    memory and claim() may be retried later. Only a missing file starts empty; damaged bookkeeping cannot prove
//    that another window's slot is free and is preserved for explicit recovery with all viewer processes closed.
//  * Saved positions are stored by the settings writer (keyed by slot) and are not removed when a window closes.
class WindowSlot {
public:
    explicit WindowSlot(const QString& directory = {}, int timeoutMs = 3000);
    // Safe-mode owner is captured by claims and every value-owned recovery job.
    WindowSlot(const QString& directory, int timeoutMs, std::shared_ptr<const void> lifetime);
    ~WindowSlot();
    WindowSlot(const WindowSlot&) = delete;
    WindowSlot& operator=(const WindowSlot&) = delete;
    int number() const { return number_; }
    // Retries the claim after a failed one; true when this window holds a slot.
    // timeoutMs=0 never waits for contention or retries file replacement (interactive callers).
    bool claim(int timeoutMs = 3000);
    // SAFE-MODE-01: immutable per-claim identity. A new random nonce distinguishes
    // release/reclaim in the same process. Missing/unverifiable identity returns
    // an empty capability; numeric slots alone never authorize recovery writes.
    // Capture on the owner thread; the returned value may be used off-thread.
    glancelot::WindowClaim claimIdentity() const;
    void release();
private:
    QString directory_;
    int number_{};
    std::shared_ptr<glancelot::detail::WindowClaimState> claimState_;
    std::shared_ptr<const void> lifetime_;
};
