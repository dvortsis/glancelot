#pragma once
// The decoder worker hands pixels back through a QSharedMemory segment. On Windows QSharedMemory guards attach, detach and
// create with a named system semaphore (a Windows semaphore with count 1), and a semaphore has no owner: a worker killed
// (or crashed) while it holds the count, inside its own attach() or detach(), never gives it back. Whoever then calls
// lock() or detach() on that segment waits forever. The viewer used to do exactly that on the GUI thread, from
// cancel()/cancelLookahead() right after it killed the worker (STACK-CONFIRM round 3, R3-B2: a permanent window freeze, seen
// in 2 of 250 slideshow runs, stack ending in QSharedMemory::~QSharedMemory -> detach -> QSystemSemaphore acquire).
//
// Rules that keep the GUI thread out of that wait:
//   - the GUI thread never calls lock()/unlock() on a transfer segment: the worker finished writing before it sent the message
//     that makes the viewer read, so the pixels are complete without it;
//   - the GUI thread never destroys an attached segment: SharedMemoryReaper hands it to a short-lived thread that does. If a
//     dead worker did leave the semaphore at zero, only that thread blocks (the key is unique per transfer, so nothing else
//     shares the semaphore); the number of such threads is capped and beyond the cap the segment is deliberately leaked
//     instead of starting another thread, so decoder lifetime and thread count stay bounded.
#include <QDebug>
#include <QSharedMemory>
#include <atomic>
#include <memory>
#include <thread>

namespace glancelot::detail {
inline constexpr int MaxPendingSharedMemoryReaps = 16;
inline std::atomic<int>& pendingSharedMemoryReaps() { static std::atomic<int> count{0}; return count; }
inline std::atomic<int>& leakedSharedMemorySegments() { static std::atomic<int> count{0}; return count; }

struct SharedMemoryReaper {
    void operator()(QSharedMemory* memory) const noexcept {
        if (!memory) return;
        // Never attached (create failed): the destructor does not touch the semaphore.
        if (!memory->isAttached()) { delete memory; return; }
        auto& pending = pendingSharedMemoryReaps();
        if (pending.fetch_add(1) >= MaxPendingSharedMemoryReaps) {
            pending.fetch_sub(1);
            leakedSharedMemorySegments().fetch_add(1);
            qWarning("Decoder transfer segment leaked: %d teardown threads are already blocked on dead workers.", MaxPendingSharedMemoryReaps);
            return;
        }
        try {
            std::thread([memory] { delete memory; pendingSharedMemoryReaps().fetch_sub(1); }).detach();
        } catch (...) {
            pending.fetch_sub(1);
            leakedSharedMemorySegments().fetch_add(1);
        }
    }
};
using TransferPtr = std::unique_ptr<QSharedMemory, SharedMemoryReaper>;
inline TransferPtr makeTransfer(const QString& key) { return TransferPtr(new QSharedMemory(key)); }
}
