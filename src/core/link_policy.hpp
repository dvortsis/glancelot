#pragma once
// Private core helper (not a public contract): the one link/reparse-point policy shared by traversal
// (navigation.cpp) and the mixed directory listing (directory_listing.cpp).
#include <filesystem>
#include <stop_token>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glancelot::detail {
// True for symlinks and, on Windows, every reparse point (junctions, volume mounts, cloud placeholders).
// An unreadable entry is treated as a link, so automatic traversal never follows what it cannot classify.
inline bool isLink(const std::filesystem::path& p) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(p, ec);
    if (ec || std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}
// No component of `p` (after the root/share) is a link.
inline bool safePath(const std::filesystem::path& p, std::stop_token cancel = {}) {
    std::filesystem::path prefix;
    for (const auto& part : p) {
        if (cancel.stop_requested()) return false;
        prefix /= part;
        // A UNC server root (\\server\) is not a listable share. Validate from the
        // complete share component onward, rather than rejecting all UNC paths here.
        if (prefix == p.root_name() || (p.has_root_name() && prefix == p.root_path())) continue;
        if (isLink(prefix)) return false;
    }
    return true;
}
}
