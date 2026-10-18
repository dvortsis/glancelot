#include "glancelot/navigation.hpp"
#include <algorithm>
#include <fstream>
#include <string_view>
#include <unordered_map>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace glancelot {
namespace {
namespace fs = std::filesystem;
using Native = fs::path::string_type;
using Unit = fs::path::value_type;
Unit fold(Unit c) { return c >= Unit('A') && c <= Unit('Z') ? Unit(c + ('a' - 'A')) : c; }
bool digit(Unit c) { return c >= Unit('0') && c <= Unit('9'); }
// Pinned portable policy: fold ASCII only; all other native units retain ordinal order.
// Numeric runs have unlimited precision; leading-zero/case ties use raw names last.
int compareNames(const Native& a, const Native& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            auto ae = i, be = j;
            while (ae < a.size() && digit(a[ae])) ++ae;
            while (be < b.size() && digit(b[be])) ++be;
            auto az = i, bz = j;
            while (az < ae && a[az] == Unit('0')) ++az;
            while (bz < be && b[bz] == Unit('0')) ++bz;
            if (ae - az != be - bz) return ae - az < be - bz ? -1 : 1;
            for (; az < ae; ++az, ++bz) {
                if (a[az] != b[bz]) return a[az] < b[bz] ? -1 : 1;
            }
            i = ae; j = be;
        } else {
            const auto ac = fold(a[i]), bc = fold(b[j]);
            if (ac != bc) return ac < bc ? -1 : 1;
            ++i; ++j;
        }
    }
    if (i != a.size() || j != b.size()) return i == a.size() ? -1 : 1;
    return 0;
}
bool sameComponent(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    const auto& x = a.native();
    const auto& y = b.native();
    return CompareStringOrdinal(x.data(), static_cast<int>(x.size()),
        y.data(), static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
#else
    return a == b;
#endif
}
bool inside(const fs::path& root, const fs::path& path) {
    auto r = root.begin(), p = path.begin();
    for (; r != root.end(); ++r, ++p)
        if (p == path.end() || !sameComponent(*r, *p)) return false;
    return true;
}
fs::path absolutePath(const fs::path& p, std::error_code& ec) {
    if (p.empty()) { ec = std::make_error_code(std::errc::invalid_argument); return {}; }
    return fs::absolute(p, ec).lexically_normal();
}
bool link(const fs::path& p) {
    std::error_code ec;
    const auto status = fs::symlink_status(p, ec);
    if (ec || fs::is_symlink(status)) return true;
#ifdef _WIN32
    // Junctions and other reparse points are also excluded, including volume mounts.
    const auto attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}
bool safePath(const fs::path& p, std::stop_token cancel = {}) {
    fs::path prefix;
    for (const auto& part : p) {
        if (cancel.stop_requested()) return false;
        prefix /= part;
        if (prefix == p.root_name()) continue;
        if (link(prefix)) return false;
    }
    return true;
}
struct ScanCancelled {};
using MetaMap = std::unordered_map<fs::path::string_type, FileMeta>;
std::vector<fs::path> entries(const fs::path& directory, bool directories, std::stop_token cancel,
                              std::size_t limit = static_cast<std::size_t>(-1), bool* truncated = nullptr, MetaMap* meta = nullptr) {
    std::vector<fs::path> result;
    if (cancel.stop_requested() || !safePath(directory, cancel)) return result;
    std::error_code ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        if (cancel.stop_requested()) return {};
        const auto path = it->path();
        const auto status = it->symlink_status(ec);
        bool sameVolume = true;
#ifndef _WIN32
        if (directories && !ec && fs::is_directory(status)) {
            struct stat parentDevice {}, childDevice {};
            sameVolume = ::stat(directory.c_str(), &parentDevice) == 0
                && ::stat(path.c_str(), &childDevice) == 0 && parentDevice.st_dev == childDevice.st_dev;
        }
#endif
        if (!ec && sameVolume && !link(path) && (directories ? fs::is_directory(status) :
            (fs::is_regular_file(status) && isSupportedImage(path)))) {
            if (result.size() >= limit) { if (truncated) *truncated = true; break; }
            result.push_back(path);
            if (meta && !directories) {
                // Directory iteration usually carries these already (Windows), so this is not a second pass of stats.
                std::error_code sizeError, timeError;
                const auto size = it->file_size(sizeError);
                const auto time = it->last_write_time(timeError);
                (*meta)[path.native()] = FileMeta{sizeError || timeError ? 0 : std::int64_t(time.time_since_epoch().count()),
                                                  sizeError ? 0 : size, !sizeError && !timeError};
            }
        }
        ec.clear();
        it.increment(ec);
    }
    if (cancel.stop_requested()) return {};
    try {
        std::sort(result.begin(), result.end(), [cancel](const fs::path& a, const fs::path& b) {
            if (cancel.stop_requested()) throw ScanCancelled{};
            return naturalLess(a, b);
        });
    } catch (const ScanCancelled&) { return {}; }
    if (cancel.stop_requested()) return {};
    return result;
}
std::optional<fs::path> adjacentDirectory(const fs::path& root, fs::path current, int direction, std::stop_token cancel) {
    if (cancel.stop_requested()) return std::nullopt;
    if (direction > 0) {
        const auto children = entries(current, true, cancel);
        if (cancel.stop_requested()) return std::nullopt;
        if (!children.empty()) return children.front();
        while (!inside(current, root)) {
            if (cancel.stop_requested()) return std::nullopt;
            const auto parent = current.parent_path();
            const auto siblings = entries(parent, true, cancel);
            if (cancel.stop_requested()) return std::nullopt;
            const auto next = std::upper_bound(siblings.begin(), siblings.end(), current, naturalLess);
            if (next != siblings.end()) return *next;
            if (parent == current || !inside(root, parent)) break;
            current = parent;
        }
    } else if (!inside(current, root)) {
        const auto parent = current.parent_path();
        const auto siblings = entries(parent, true, cancel);
        if (cancel.stop_requested()) return std::nullopt;
        auto previous = std::lower_bound(siblings.begin(), siblings.end(), current, naturalLess);
        if (previous == siblings.begin()) return parent;
        current = *--previous;
        for (;;) {
            if (cancel.stop_requested()) return std::nullopt;
            const auto children = entries(current, true, cancel);
            if (cancel.stop_requested()) return std::nullopt;
            if (children.empty()) return current;
            current = children.back();
        }
    }
    return std::nullopt;
}
}

bool naturalLess(const fs::path& a, const fs::path& b) {
    const auto an = a.filename().native(), bn = b.filename().native();
    const auto comparison = compareNames(an, bn);
    if (comparison != 0) return comparison < 0;
    if (an != bn) return an < bn;
    return a.native() < b.native();
}
bool isSupportedImage(const fs::path& path) {
    auto extension = path.extension().native();
    std::transform(extension.begin(), extension.end(), extension.begin(), fold);
    return extension == fs::path(".jpg").native() || extension == fs::path(".jpeg").native()
        || extension == fs::path(".png").native();
}
std::vector<fs::path> imageFiles(const fs::path& directory, std::stop_token cancel) { return entries(directory, false, cancel); }
namespace {
std::uint64_t shuffleHash(const fs::path& path, std::uint64_t seed) {
    std::uint64_t value = 1469598103934665603ULL ^ seed; // FNV-1a over the file name, then a splitmix64 finalizer
    const auto name = path.filename(); // keep the temporary alive for the loop
    for (const auto unit : name.native()) { value ^= std::uint64_t(unit); value *= 1099511628211ULL; }
    value += seed * 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}
bool sortBefore(const SortSpec& sort, const fs::path& a, const FileMeta& ma, const fs::path& b, const FileMeta& mb) {
    if (sort.key == SortKey::Name) return sort.descending ? naturalLess(b, a) : naturalLess(a, b);
    int order = 0;
    if (sort.key == SortKey::Shuffle) {
        const auto ha = shuffleHash(a, sort.seed), hb = shuffleHash(b, sort.seed);
        order = ha < hb ? -1 : ha > hb ? 1 : 0;
    } else {
        if (ma.known != mb.known) return ma.known; // unreadable metadata is last in both directions
        if (ma.known) {
            if (sort.key == SortKey::Modified) order = ma.modified < mb.modified ? -1 : ma.modified > mb.modified ? 1 : 0;
            else order = ma.size < mb.size ? -1 : ma.size > mb.size ? 1 : 0;
        }
    }
    if (order != 0) return sort.descending ? order > 0 : order < 0;
    return naturalLess(a, b);
}
}
bool fileSortBefore(const SortSpec& sort, const fs::path& a, const FileMeta& ma, const fs::path& b, const FileMeta& mb) { return sortBefore(sort, a, ma, b, mb); }
std::vector<fs::path> sortedImageFiles(const fs::path& directory, const SortSpec& sort, std::stop_token cancel, std::vector<FileMeta>* meta) {
    MetaMap map;
    const bool needMeta = sort.key == SortKey::Modified || sort.key == SortKey::Size;
    auto files = entries(directory, false, cancel, static_cast<std::size_t>(-1), nullptr, needMeta ? &map : nullptr);
    if (cancel.stop_requested()) return {};
    const auto lookup = [&](const fs::path& path) { const auto found = map.find(path.native()); return found == map.end() ? FileMeta{} : found->second; };
    if (sort.key == SortKey::Name) {
        // entries() already returns natural order, a strict total order, so descending is exactly its reverse.
        if (sort.descending) std::reverse(files.begin(), files.end());
    } else {
        try {
            std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
                if (cancel.stop_requested()) throw ScanCancelled{};
                return sortBefore(sort, a, lookup(a), b, lookup(b));
            });
        } catch (const ScanCancelled&) { return {}; }
    }
    if (cancel.stop_requested()) return {};
    if (meta) { meta->clear(); meta->reserve(files.size()); for (const auto& file : files) meta->push_back(lookup(file)); }
    return files;
}
FolderListing childFolders(const fs::path& directory, std::stop_token cancel, std::size_t limit) {
    FolderListing listing;
    if (cancel.stop_requested()) { listing.cancelled = true; return listing; }
    std::error_code ec;
    // A separate probe without skip_permission_denied distinguishes "cannot list" from "no subfolders".
    if (!safePath(directory, cancel)) { listing.cancelled = cancel.stop_requested(); listing.accessible = false; return listing; }
    { fs::directory_iterator probe(directory, ec); if (ec) { listing.accessible = false; return listing; } }
    listing.folders = entries(directory, true, cancel, limit, &listing.truncated);
    if (cancel.stop_requested()) { listing.folders.clear(); listing.cancelled = true; listing.truncated = false; }
    return listing;
}
SubtreeImage firstImageInSubtree(const fs::path& directory, std::stop_token cancel, std::size_t maxFolders) {
    SubtreeImage result;
    if (cancel.stop_requested()) return result;
    std::error_code ec;
    if (!safePath(directory, cancel)) { if (!cancel.stop_requested()) result.accessible = false; return result; }
    { fs::directory_iterator probe(directory, ec); if (ec) { result.accessible = false; return result; } }
    std::vector<fs::path> pending{directory};
    std::size_t visited = 0;
    while (!pending.empty()) {
        if (cancel.stop_requested()) return SubtreeImage{};
        if (visited++ >= maxFolders) { result.exhausted = true; return result; }
        const auto current = pending.back();
        pending.pop_back();
        const auto files = imageFiles(current, cancel);
        if (cancel.stop_requested()) return SubtreeImage{};
        if (!files.empty()) { result.image = files.front(); return result; }
        const auto children = entries(current, true, cancel, 20000);
        if (cancel.stop_requested()) return SubtreeImage{};
        // Reversed so the natural-order first child is visited first (preorder).
        for (auto child = children.rbegin(); child != children.rend(); ++child) pending.push_back(*child);
    }
    return result;
}
std::optional<fs::path> adjacentFolderImage(const fs::path& rootPath, const fs::path& directory, int direction, std::stop_token cancel) {
    if (direction == 0 || cancel.stop_requested()) return std::nullopt;
    std::error_code ec;
    const auto root = absolutePath(rootPath, ec);
    if (ec) return std::nullopt;
    const auto initial = absolutePath(directory, ec);
    if (ec || !inside(root, initial) || !safePath(root, cancel)) return std::nullopt;
    // A deleted current directory is a valid ordering anchor, but a live link is not.
    if (fs::exists(initial, ec) && !safePath(initial, cancel)) return std::nullopt;
    auto next = adjacentDirectory(root, initial, direction, cancel);
    while (next) {
        if (cancel.stop_requested()) return std::nullopt;
        const auto files = imageFiles(*next, cancel);
        if (cancel.stop_requested()) return std::nullopt;
        if (!files.empty()) return direction > 0 ? files.front() : files.back();
        next = adjacentDirectory(root, *next, direction, cancel);
    }
    return std::nullopt;
}

bool Navigator::open(const fs::path& file, std::error_code& error) {
    error.clear();
    const auto cancelled = [&] {
        if (!cancel_.stop_requested()) return false;
        error = std::make_error_code(std::errc::operation_canceled);
        return true;
    };
    if (cancelled()) return false;
    const auto path = absolutePath(file, error);
    if (error) return false;
    if (!isSupportedImage(path)) { error = std::make_error_code(std::errc::not_supported); return false; }
    if (!safePath(path, cancel_)) {
        if (cancelled()) return false;
        error = std::make_error_code(std::errc::no_such_file_or_directory); return false; }
    const bool regular = fs::is_regular_file(path, error);
    if (cancelled()) return false;
    if (!regular) {
        if (!error) error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    std::ifstream readable(path, std::ios::binary);
    if (cancelled()) return false;
    if (!readable) { error = std::make_error_code(std::errc::permission_denied); return false; }
    std::vector<fs::path> files; std::vector<FileMeta> meta;
    list(path.parent_path(), files, meta);
    if (cancelled()) return false;
    const auto selected = std::find_if(files.begin(), files.end(), [&](const fs::path& candidate) {
        return cancel_.stop_requested() || sameComponent(candidate.filename(), path.filename());
    });
    if (cancelled()) return false;
    if (selected == files.end()) {
        error = std::make_error_code(std::errc::no_such_file_or_directory); return false;
    }
    if (cancelled()) return false;
    state_.root = state_.directory = path.parent_path();
    state_.current = *selected;
    anchor_ = meta[std::size_t(selected - files.begin())];
    state_.files = std::move(files);
    meta_ = std::move(meta);
    ++state_.generation;
    return true;
}
bool Navigator::setRoot(const fs::path& root) {
    if (state_.current.empty() || cancel_.stop_requested()) return false;
    std::error_code ec;
    const auto path = absolutePath(root, ec);
    if (ec || !inside(path, state_.root) || !safePath(path, cancel_) || !fs::is_directory(path, ec) || ec || cancel_.stop_requested()) return false;
    state_.root = path;
    ++state_.generation;
    return true;
}
void Navigator::setBoundary(BoundaryMode mode) { state_.boundary = mode; }
std::optional<fs::path> Navigator::step(int direction) {
    if (direction == 0 || state_.current.empty() || cancel_.stop_requested()) return std::nullopt;
    refresh();
    if (cancel_.stop_requested()) return std::nullopt;
    const auto& files = state_.files;
    // The current image, or, when it was deleted, the position its remembered sort key would have had.
    auto found = std::find(files.begin(), files.end(), state_.current);
    if (found == files.end())
        found = std::partition_point(files.begin(), files.end(), [&](const fs::path& file) {
            return sortBefore(sort_, file, meta_[std::size_t(&file - files.data())], state_.current, anchor_);
        });
    std::optional<fs::path> destination;
    if (direction > 0) {
        if (found != files.end() && *found == state_.current) ++found;
        if (found != files.end()) destination = *found;
    } else if (found != files.begin()) destination = *--found;
    if (!destination && state_.boundary == BoundaryMode::LoopFolder && !files.empty())
        destination = direction > 0 ? files.front() : files.back();
    if (!destination && state_.boundary == BoundaryMode::ContinueTree) return enterFolder(direction, true);
    if (cancel_.stop_requested()) return std::nullopt;
    if (destination) {
        state_.current = *destination;
        const auto at = std::find(files.begin(), files.end(), *destination);
        if (at != files.end()) anchor_ = meta_[std::size_t(at - files.begin())];
        ++state_.generation;
    }
    return destination;
}
std::optional<fs::path> Navigator::folder(int direction) { return enterFolder(direction, false); }
std::optional<fs::path> Navigator::enterFolder(int direction, bool sortedLanding) {
    if (state_.current.empty() || cancel_.stop_requested()) return std::nullopt;
    const auto destination = adjacentFolderImage(state_.root, state_.directory, direction, cancel_);
    if (!destination) return std::nullopt;
    std::vector<fs::path> files; std::vector<FileMeta> meta;
    list(destination->parent_path(), files, meta);
    if (cancel_.stop_requested()) return std::nullopt;
    state_.directory = destination->parent_path();
    // An explicit folder jump lands on the natural endpoint chosen by traversal, whatever the file sort (NAV-02).
    // Crossing a folder boundary while stepping continues the visible order instead, so no image is skipped.
    fs::path landing = *destination;
    if (sortedLanding && !files.empty()) landing = direction > 0 ? files.front() : files.back();
    const auto at = std::find(files.begin(), files.end(), landing);
    anchor_ = at == files.end() ? FileMeta{} : meta[std::size_t(at - files.begin())];
    state_.files = std::move(files);
    meta_ = std::move(meta);
    state_.current = landing;
    ++state_.generation;
    return landing;
}
void Navigator::refresh() {
    if (state_.directory.empty() || cancel_.stop_requested()) return;
    std::vector<fs::path> files; std::vector<FileMeta> meta;
    list(state_.directory, files, meta);
    if (cancel_.stop_requested()) return;
    // A deleted current keeps its last known sort key as the anchor for deterministic next/previous.
    const auto at = std::find(files.begin(), files.end(), state_.current);
    if (at != files.end()) anchor_ = meta[std::size_t(at - files.begin())];
    state_.files = std::move(files);
    meta_ = std::move(meta);
    ++state_.generation;
}
void Navigator::list(const fs::path& directory, std::vector<fs::path>& files, std::vector<FileMeta>& meta) {
    files = sortedImageFiles(directory, sort_, cancel_, &meta);
}
void Navigator::setSort(const SortSpec& sort) {
    if (sort == sort_) return;
    sort_ = sort;
    refresh();
}
}
