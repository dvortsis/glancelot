#include "glancelot/navigation.hpp"
#include <algorithm>
#include <fstream>
#include <string_view>
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
std::vector<fs::path> entries(const fs::path& directory, bool directories, std::stop_token cancel) {
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
            (fs::is_regular_file(status) && isSupportedImage(path)))) result.push_back(path);
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
FolderListing childFolders(const fs::path& directory, std::stop_token cancel) {
    FolderListing listing;
    if (cancel.stop_requested()) { listing.cancelled = true; return listing; }
    std::error_code ec;
    // A separate probe without skip_permission_denied distinguishes "cannot list" from "no subfolders".
    if (!safePath(directory, cancel)) { listing.cancelled = cancel.stop_requested(); listing.accessible = false; return listing; }
    { fs::directory_iterator probe(directory, ec); if (ec) { listing.accessible = false; return listing; } }
    listing.folders = entries(directory, true, cancel);
    if (cancel.stop_requested()) { listing.folders.clear(); listing.cancelled = true; }
    return listing;
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
    auto files = imageFiles(path.parent_path(), cancel_);
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
    state_.files = std::move(files);
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
    auto found = std::lower_bound(files.begin(), files.end(), state_.current, naturalLess);
    std::optional<fs::path> destination;
    if (direction > 0) {
        if (found != files.end() && *found == state_.current) ++found;
        if (found != files.end()) destination = *found;
    } else if (found != files.begin()) destination = *--found;
    if (!destination && state_.boundary == BoundaryMode::LoopFolder && !files.empty())
        destination = direction > 0 ? files.front() : files.back();
    if (!destination && state_.boundary == BoundaryMode::ContinueTree) return folder(direction);
    if (cancel_.stop_requested()) return std::nullopt;
    if (destination) { state_.current = *destination; ++state_.generation; }
    return destination;
}
std::optional<fs::path> Navigator::folder(int direction) {
    if (state_.current.empty() || cancel_.stop_requested()) return std::nullopt;
    const auto destination = adjacentFolderImage(state_.root, state_.directory, direction, cancel_);
    if (!destination) return std::nullopt;
    auto files = imageFiles(destination->parent_path(), cancel_);
    if (cancel_.stop_requested()) return std::nullopt;
    state_.directory = destination->parent_path();
    state_.files = std::move(files);
    state_.current = *destination;
    ++state_.generation;
    return destination;
}
void Navigator::refresh() {
    if (state_.directory.empty() || cancel_.stop_requested()) return;
    auto files = imageFiles(state_.directory, cancel_);
    if (cancel_.stop_requested()) return;
    state_.files = std::move(files);
    // Preserve a deleted current name as an anchor for deterministic next/previous.
    ++state_.generation;
}
}
