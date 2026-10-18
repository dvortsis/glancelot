#include "glancelot/directory_listing.hpp"
#include "link_policy.hpp"
#include <algorithm>

// Mixed folder listing for the file-manager grid. One directory level, one pass, worker thread only.
// Classification matches what traversal accepts: an Image is a regular, non-link file with a supported
// extension (exactly what Navigator::open admits). Links of any kind are listed with `link` set and are never
// classified as images, so their targets are never decoded or thumbnailed through the grid.
namespace glancelot {
namespace {
namespace fs = std::filesystem;
struct Cancelled {};
#ifndef _WIN32
bool dotName(const fs::path& name) { const auto& n = name.native(); return !n.empty() && n.front() == '.'; }
#endif
// Folders first in natural order, files in the viewer's order. Cancellable during the sort itself.
bool finish(std::vector<DirectoryEntry>& entries, const SortSpec& sort, std::stop_token cancel) {
    const auto split = std::stable_partition(entries.begin(), entries.end(), [](const DirectoryEntry& e) { return e.kind == EntryKind::Folder; });
    try {
        std::sort(entries.begin(), split, [&](const DirectoryEntry& a, const DirectoryEntry& b) {
            if (cancel.stop_requested()) throw Cancelled{};
            return naturalLess(a.path, b.path);
        });
        std::sort(split, entries.end(), [&](const DirectoryEntry& a, const DirectoryEntry& b) {
            if (cancel.stop_requested()) throw Cancelled{};
            return fileSortBefore(sort, a.path, a.meta, b.path, b.meta);
        });
    } catch (const Cancelled&) { return false; }
    return !cancel.stop_requested();
}
}

DirectoryListing listDirectory(const fs::path& directory, const SortSpec& sort, bool includeHidden, std::stop_token cancel, std::size_t limit) {
    DirectoryListing result;
    const auto cancelled = [&] { result = DirectoryListing{}; result.cancelled = true; return result; };
    if (cancel.stop_requested()) return cancelled();
    // Same policy as the tree and traversal: a folder reached through a link is not listed.
    if (directory.empty() || !detail::safePath(directory, cancel)) {
        if (cancel.stop_requested()) return cancelled();
        result.accessible = false; return result;
    }
    std::vector<DirectoryEntry>& entries = result.entries;
    const auto admit = [&](DirectoryEntry&& entry) {
        if (entry.hidden && !includeHidden) return true;
        if (entries.size() >= limit) { result.truncated = true; return false; }
        entries.push_back(std::move(entry));
        return true;
    };
#ifdef _WIN32
    // FindFirstFileEx returns attributes, size and time with each name: no per-entry stat.
    WIN32_FIND_DATAW data{};
    const auto pattern = directory / L"*";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        // A drive root may legitimately contain nothing at all; everything else is "cannot list".
        if (GetLastError() != ERROR_FILE_NOT_FOUND) result.accessible = false;
        return result;
    }
    do {
        if (cancel.stop_requested()) { FindClose(find); return cancelled(); }
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        const DWORD attributes = data.dwFileAttributes;
        if (attributes & FILE_ATTRIBUTE_DEVICE) continue;
        DirectoryEntry entry;
        entry.path = directory / data.cFileName;
        entry.link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entry.hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) || name.front() == L'.';
        const bool folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.kind = folder ? EntryKind::Folder : (!entry.link && isSupportedImage(entry.path) ? EntryKind::Image : EntryKind::File);
        // FILETIME ticks are file_clock ticks on MSVC, so ordering matches Navigator's metadata exactly.
        entry.meta = FileMeta{std::int64_t((std::uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime),
                              std::uintmax_t((std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow), true};
        if (!admit(std::move(entry))) break;
    } while (FindNextFileW(find, &data));
    FindClose(find);
#else
    std::error_code ec;
    // No skip_permission_denied: an inaccessible folder must not look empty.
    fs::directory_iterator it(directory, ec), end;
    if (ec) { result.accessible = false; return result; }
    while (it != end) {
        if (cancel.stop_requested()) return cancelled();
        DirectoryEntry entry;
        entry.path = it->path();
        std::error_code statusError;
        const auto own = it->symlink_status(statusError);
        bool keep = !statusError;
        if (keep) {
            entry.link = fs::is_symlink(own);
            const auto target = entry.link ? it->status(statusError) : own;
            if (fs::is_directory(target) && !statusError) entry.kind = EntryKind::Folder;
            else if (entry.link && (statusError || fs::is_regular_file(target))) entry.kind = EntryKind::File; // dangling links stay visible
            else if (!entry.link && fs::is_regular_file(target)) entry.kind = isSupportedImage(entry.path) ? EntryKind::Image : EntryKind::File;
            else keep = false; // devices, sockets, FIFOs (and links to them)
        }
        if (keep) {
            entry.hidden = dotName(entry.path.filename());
            std::error_code sizeError, timeError;
            const auto size = entry.kind == EntryKind::Folder ? std::uintmax_t{} : it->file_size(sizeError);
            const auto time = it->last_write_time(timeError);
            entry.meta = FileMeta{timeError ? 0 : std::int64_t(time.time_since_epoch().count()), sizeError ? 0 : size, !sizeError && !timeError};
            if (!admit(std::move(entry))) break;
        }
        it.increment(ec);
        if (ec) break; // an entry vanished mid-listing or the directory went away: keep what was read
    }
#endif
    if (!finish(entries, sort, cancel)) return cancelled();
    return result;
}
}
