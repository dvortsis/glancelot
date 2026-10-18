#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

// Lead-frozen DELETE-01 core contract: explicit, user-initiated deletion to the Trash/Recycle Bin.
// Worker-thread only; never called from UI/render code. Viewing never reaches this code path.
//  * Targets are snapshotted (identity + metadata) when the user asks, and revalidated immediately before each
//    mutation. A target that was replaced, changed or vanished in between is reported, never deleted.
//  * Links are targets in their own right: the link itself is trashed, its target never is, and directory links
//    are never followed when counting folder contents.
//  * There is no permanent-delete path. A TrashBackend that cannot move an item to the trash must fail that item.
namespace glancelot {
// Stable on-disk identity, read without following links: (volume serial, file index) on Windows, (dev, ino) on POSIX.
struct FileIdentity {
    std::uint64_t volume{}, file{};
    bool operator==(const FileIdentity&) const = default;
};
enum class TargetKind { File, Folder };
struct MutationTarget {
    std::filesystem::path path;      // absolute, lexically normal; as shown to the user
    TargetKind kind{TargetKind::File};
    bool link{};                     // symlink / junction / other reparse point: the link alone is the target
    FileIdentity identity;
    std::int64_t modified{};         // file_time ticks; revalidated for File targets only
    std::uintmax_t bytes{};          // File targets only
    // Folder targets: bounded recursive count of contents without following links (for the confirmation).
    std::uint64_t contentFiles{}, contentFolders{};
    bool contentCountComplete{true}; // false when the bound or an unreadable subfolder stopped the count
};
enum class PlanRejection { Missing, Root, Duplicate, Inaccessible, ReadOnlyVolume, Special };
struct RejectedTarget { std::filesystem::path path; PlanRejection reason; std::string message; };
struct DeletionPlan {
    std::vector<MutationTarget> targets;   // deduplicated: a descendant of another target is dropped (Duplicate)
    std::vector<RejectedTarget> rejected;  // shown in the confirmation; never mutated
    bool cancelled{};
};
// Snapshot `requested` paths. Filesystem roots (drive roots, UNC share roots, "/") are rejected. Paired/sidecar
// files are never added implicitly: the plan contains exactly the requested items (explicit paired-file policy).
// `contentLimit` bounds the per-plan folder-content walk (entries visited in total).
DeletionPlan planDeletion(const std::vector<std::filesystem::path>& requested, std::stop_token cancel = {},
                          std::uint64_t contentLimit = 100000);

enum class DeleteOutcome {
    Trashed,          // moved to the Trash/Recycle Bin
    Missing,          // gone before the mutation; nothing done
    Changed,          // identity or (file) size/modified differs from the snapshot; nothing done
    Denied,           // permission / read-only
    InUse,            // locked or sharing violation
    TrashUnavailable, // the volume has no usable trash; NOT deleted permanently
    Cancelled,        // not attempted because cancellation was requested first
    Failed            // any other error; message carries the reason
};
struct DeleteResult { std::filesystem::path path; DeleteOutcome outcome{DeleteOutcome::Failed}; std::string message; };

// Platform trash. Implementations must never fall back to permanent deletion and must not follow links.
// Tests substitute a backend to simulate unavailable trash, locks and failures.
struct TrashBackend {
    virtual ~TrashBackend() = default;
    virtual DeleteResult moveToTrash(const MutationTarget& target) = 0;
};
// Current identity/metadata for revalidation; std::nullopt when the path no longer exists.
std::optional<MutationTarget> snapshotTarget(const std::filesystem::path& path, std::error_code& error);
// Revalidates and trashes each target in order. Cancellation is honoured between items (an item already handed to
// the backend completes). Returns one result per plan target, in plan order. `progress(done, total)` is optional.
std::vector<DeleteResult> executeDeletion(const DeletionPlan& plan, TrashBackend& backend, std::stop_token cancel = {},
                                          const std::function<void(std::size_t, std::size_t)>& progress = {});
}
