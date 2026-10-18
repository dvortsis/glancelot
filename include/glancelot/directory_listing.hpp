#pragma once
#include "navigation.hpp"

// Lead-frozen FILE-GRID-01 core contract. Worker-thread only; never called from UI/render code.
namespace glancelot {
enum class EntryKind { Folder, Image, File };
struct DirectoryEntry {
    std::filesystem::path path;
    EntryKind kind{EntryKind::File};
    bool link{};          // symlink/junction; directory links are listed but never entered automatically
    bool hidden{};        // dot-file, or the Windows hidden attribute
    FileMeta meta;        // size/modified; known=false when unreadable
};
// `entries`: folders first in natural order, then every regular file (images and others) in `sort` order.
// Unsupported files are listed by type only; their contents are never opened. Special files (devices, sockets,
// FIFOs) are omitted. `accessible` is false when the directory itself cannot be listed. A cancelled listing carries
// no entries. `truncated` means more than `limit` entries exist; the returned entries are then a bounded subset.
struct DirectoryListing {
    std::vector<DirectoryEntry> entries;
    bool accessible{true}, cancelled{false}, truncated{false};
};
DirectoryListing listDirectory(const std::filesystem::path& directory, const SortSpec& sort,
                               bool includeHidden = false, std::stop_token cancel = {}, std::size_t limit = 100000);
}
