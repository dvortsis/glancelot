#include "glancelot/file_operations.hpp"
#include "link_policy.hpp"
#include <algorithm>
#include <cwctype>
#include <exception>
#ifdef _WIN32
#include <vector>
#else
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

// DELETE-01 core: snapshot, planning and revalidated execution of user-initiated moves to the Trash/Recycle Bin.
// Every filesystem read here is no-follow: the leaf is opened as itself (FILE_FLAG_OPEN_REPARSE_POINT / lstat) and
// every ancestor must be link-free (detail::safePath), so a request can never reach a link's target. This file has
// no mutation of its own: the only mutation is TrashBackend::moveToTrash, called by executeDeletion after the
// target has just been revalidated.
namespace glancelot {
namespace {
namespace fs = std::filesystem;

// Snapshot plus the facts only planning needs.
struct Probe {
    MutationTarget target;
    bool special{};          // device, socket, FIFO, console name ...
    bool root{};             // the item is itself the root of a volume or share
    bool readOnlyVolume{};
};

// Absolute, lexically normal, no trailing separator except on a bare root, preferred separators.
fs::path normal(const fs::path& path) {
    auto p = path.lexically_normal();
    p.make_preferred();
    while (!p.has_filename() && p.has_relative_path()) p = p.parent_path();
    return p;
}
#ifdef _WIN32
std::wstring fold(const fs::path& path) {
    std::wstring key = path.native();
    for (auto& c : key) c = wchar_t(std::towlower(c));
    return key;
}
#else
std::string fold(const fs::path& path) { return path.native(); }
#endif
// `a` is `b` or an ancestor of `b`, on normal() paths.
bool sameOrAncestor(const fs::path& a, const fs::path& b) {
    const auto ka = fold(a), kb = fold(b);
    if (ka == kb) return true;
    if (kb.size() <= ka.size() || kb.compare(0, ka.size(), ka) != 0) return false;
    const auto sep = fs::path::preferred_separator;
    return ka.back() == sep || kb[ka.size()] == sep;
}
// Lexical roots: "/", "C:\", "\\server\share". Also device-namespace paths, which are never targets.
bool lexicalRoot(const fs::path& p) {
    if (!p.has_relative_path()) return true;
#ifdef _WIN32
    const std::wstring name = p.root_name().native();
    if (name.size() > 2 && (name[0] == L'\\' || name[0] == L'/') && (name[1] == L'\\' || name[1] == L'/')) {
        // \\server\share: the share component is the root of that filesystem.
        const fs::path relative = p.relative_path();
        return std::distance(relative.begin(), relative.end()) <= 1;
    }
#endif
    return false;
}
bool deviceNamespace(const fs::path& p) {
#ifdef _WIN32
    const auto& n = p.native();
    return n.size() >= 4 && (n[0] == L'\\' || n[0] == L'/') && (n[1] == L'\\' || n[1] == L'/') && (n[2] == L'?' || n[2] == L'.')
        && (n[3] == L'\\' || n[3] == L'/');
#else
    (void)p; return false;
#endif
}

#ifdef _WIN32
bool missingError(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME
        || error == ERROR_BAD_NETPATH || error == ERROR_BAD_NET_NAME || error == ERROR_INVALID_DRIVE;
}
// Reads the item itself (never a reparse target). nullopt + clear error: the path does not exist.
std::optional<Probe> probe(const fs::path& path, std::error_code& error, bool volumeFacts) {
    error.clear();
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD last = GetLastError();
        if (!missingError(last)) error = std::error_code(int(last), std::system_category());
        return std::nullopt;
    }
    Probe result;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool disk = GetFileType(handle) == FILE_TYPE_DISK;
    const bool ok = disk && GetFileInformationByHandle(handle, &info);
    const DWORD last = GetLastError();
    CloseHandle(handle);
    if (!disk) { result.special = true; result.target.path = path; return result; }
    if (!ok) { error = std::error_code(int(last), std::system_category()); return std::nullopt; }
    auto& t = result.target;
    t.path = path;
    t.kind = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TargetKind::Folder : TargetKind::File;
    t.link = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    t.identity = {info.dwVolumeSerialNumber, (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
    // FILETIME ticks are file_clock ticks on MSVC (the same unit directory_listing publishes).
    t.modified = std::int64_t((std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime);
    t.bytes = t.kind == TargetKind::File ? ((std::uintmax_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) : 0;
    result.special = (info.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0;
    if (volumeFacts) {
        // The volume (or mounted folder) that holds the item. An item that is its own volume path is a root.
        std::vector<wchar_t> volume(32768);
        if (GetVolumePathNameW(path.c_str(), volume.data(), DWORD(volume.size()))) {
            const fs::path volumePath = normal(fs::path(volume.data()));
            result.root = fold(volumePath) == fold(path);
            std::wstring root = volume.data();
            if (root.empty() || root.back() != L'\\') root += L'\\';
            DWORD flags = 0;
            if (GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0))
                result.readOnlyVolume = (flags & FILE_READ_ONLY_VOLUME) != 0;
        }
    }
    return result;
}
#else
std::int64_t ticks(const struct stat& st) {
#ifdef __APPLE__
    return std::int64_t(st.st_mtimespec.tv_sec) * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    return std::int64_t(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
#endif
}
std::optional<Probe> probe(const fs::path& path, std::error_code& error, bool volumeFacts) {
    error.clear();
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        const int e = errno;
        if (e != ENOENT && e != ENOTDIR) error = std::error_code(e, std::generic_category());
        return std::nullopt;
    }
    Probe result;
    auto& t = result.target;
    t.path = path;
    t.link = S_ISLNK(st.st_mode);
    t.identity = {std::uint64_t(st.st_dev), std::uint64_t(st.st_ino)};
    t.modified = ticks(st);
    if (t.link) {
        // Classify a link by what it names, for wording only; its contents are never read or counted.
        struct stat target{};
        t.kind = ::stat(path.c_str(), &target) == 0 && S_ISDIR(target.st_mode) ? TargetKind::Folder : TargetKind::File;
        t.bytes = t.kind == TargetKind::File ? std::uintmax_t(st.st_size) : 0;
    } else if (S_ISDIR(st.st_mode)) {
        t.kind = TargetKind::Folder;
    } else if (S_ISREG(st.st_mode)) {
        t.kind = TargetKind::File; t.bytes = std::uintmax_t(st.st_size);
    } else {
        result.special = true;
    }
    if (volumeFacts) {
        const auto parent = path.parent_path();
        struct stat up{};
        // A mount point lives on another device than its parent; "/" is its own parent.
        if (!t.link && S_ISDIR(st.st_mode) && ::lstat(parent.c_str(), &up) == 0)
            result.root = up.st_dev != st.st_dev || (up.st_ino == st.st_ino && up.st_dev == st.st_dev);
        struct statvfs volume{};
        if (::statvfs(parent.c_str(), &volume) == 0) result.readOnlyVolume = (volume.f_flag & ST_RDONLY) != 0;
    }
    return result;
}
#endif

// Bounded, no-follow count of everything under each folder target. Links inside count as one file and are never
// entered; an unreadable subfolder or the shared budget marks the count incomplete ("at least").
bool countContents(MutationTarget& target, std::uint64_t& budget, std::stop_token cancel) {
    std::vector<fs::path> pending{target.path};
    while (!pending.empty()) {
        if (cancel.stop_requested()) return false;
        const fs::path directory = std::move(pending.back());
        pending.pop_back();
#ifdef _WIN32
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW((directory / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) target.contentCountComplete = false;
            continue;
        }
        do {
            if (cancel.stop_requested()) { FindClose(find); return false; }
            const std::wstring_view name(data.cFileName);
            if (name == L"." || name == L"..") continue;
            if (budget == 0) { target.contentCountComplete = false; FindClose(find); return true; }
            --budget;
            const bool folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
            if (folder) { ++target.contentFolders; pending.push_back(directory / data.cFileName); }
            else ++target.contentFiles;
        } while (FindNextFileW(find, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES) target.contentCountComplete = false;
        FindClose(find);
#else
        std::error_code ec;
        fs::directory_iterator it(directory, ec), end;
        if (ec) { target.contentCountComplete = false; continue; }
        for (; it != end; it.increment(ec)) {
            if (cancel.stop_requested()) return false;
            if (budget == 0) { target.contentCountComplete = false; return true; }
            --budget;
            std::error_code statusError;
            const auto own = it->symlink_status(statusError);
            if (!statusError && fs::is_directory(own)) { ++target.contentFolders; pending.push_back(it->path()); }
            else ++target.contentFiles;
        }
        if (ec) target.contentCountComplete = false;
#endif
    }
    return true;
}
DeleteResult result(const fs::path& path, DeleteOutcome outcome, std::string message = {}) { return {path, outcome, std::move(message)}; }
}

std::optional<MutationTarget> snapshotTarget(const fs::path& path, std::error_code& error) {
    auto found = probe(normal(path), error, false);
    if (!found) return std::nullopt;
    return found->target;
}

DeletionPlan planDeletion(const std::vector<fs::path>& requested, std::stop_token cancel, std::uint64_t contentLimit) {
    DeletionPlan plan;
    const auto cancelled = [&] { plan = DeletionPlan{}; plan.cancelled = true; return plan; };
    struct Candidate { MutationTarget target; std::size_t order; };
    std::vector<Candidate> accepted;
    const auto reject = [&](const fs::path& path, PlanRejection reason, std::string message) {
        plan.rejected.push_back({path, reason, std::move(message)});
    };
    for (std::size_t i = 0; i < requested.size(); ++i) {
        if (cancel.stop_requested()) return cancelled();
        const fs::path& raw = requested[i];
        if (deviceNamespace(raw)) { reject(raw, PlanRejection::Special, "device paths are not supported"); continue; }
        if (raw.empty() || !raw.is_absolute()) { reject(raw, PlanRejection::Inaccessible, "not an absolute path"); continue; }
        const fs::path path = normal(raw);
        if (lexicalRoot(path)) { reject(path, PlanRejection::Root, "filesystem roots cannot be moved to the trash"); continue; }
        // Never through a link: every ancestor must be the real folder it names.
        if (!detail::safePath(path.parent_path(), cancel)) {
            if (cancel.stop_requested()) return cancelled();
            std::error_code parentError;
            if (!fs::exists(fs::symlink_status(path.parent_path(), parentError))) reject(path, PlanRejection::Missing, "its folder no longer exists");
            else reject(path, PlanRejection::Inaccessible, "it is reached through a link or an unreadable folder");
            continue;
        }
        std::error_code error;
        const auto found = probe(path, error, true);
        if (!found) {
            if (error) reject(path, PlanRejection::Inaccessible, error.message());
            else reject(path, PlanRejection::Missing, "it no longer exists");
            continue;
        }
        if (found->special) { reject(path, PlanRejection::Special, "special files cannot be moved to the trash"); continue; }
        if (found->root) { reject(path, PlanRejection::Root, "filesystem roots cannot be moved to the trash"); continue; }
        if (found->readOnlyVolume) { reject(path, PlanRejection::ReadOnlyVolume, "the drive is read-only"); continue; }
        accepted.push_back({found->target, i});
    }
    // Duplicates: the same item twice (same spelling or same identity), or an item inside another target.
    std::vector<bool> drop(accepted.size());
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        for (std::size_t j = 0; j < accepted.size() && !drop[i]; ++j) {
            if (i == j || drop[j]) continue;
            const auto& a = accepted[j].target; const auto& b = accepted[i].target;
            const bool same = fold(a.path) == fold(b.path) || a.identity == b.identity;
            if (same ? j < i : (a.kind == TargetKind::Folder && !a.link && sameOrAncestor(a.path, b.path))) drop[i] = true;
        }
        if (drop[i]) reject(accepted[i].target.path, PlanRejection::Duplicate, "it is already included");
    }
    std::uint64_t budget = contentLimit;
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        if (drop[i]) continue;
        auto target = accepted[i].target;
        if (target.kind == TargetKind::Folder && !target.link && !countContents(target, budget, cancel)) return cancelled();
        plan.targets.push_back(std::move(target));
    }
    if (cancel.stop_requested()) return cancelled();
    return plan;
}

std::vector<DeleteResult> executeDeletion(const DeletionPlan& plan, TrashBackend& backend, std::stop_token cancel,
                                          const std::function<void(std::size_t, std::size_t)>& progress) {
    std::vector<DeleteResult> results;
    results.reserve(plan.targets.size());
    const std::size_t total = plan.targets.size();
    for (const auto& planned : plan.targets) {
        if (cancel.stop_requested() || plan.cancelled) { results.push_back(result(planned.path, DeleteOutcome::Cancelled)); continue; }
        // Revalidate immediately before the mutation: the folder chain, then the item's own identity and metadata.
        std::error_code error;
        std::optional<MutationTarget> now;
        if (!detail::safePath(planned.path.parent_path())) {
            std::error_code parentError;
            if (!fs::exists(fs::symlink_status(planned.path.parent_path(), parentError)))
                results.push_back(result(planned.path, DeleteOutcome::Missing, "its folder no longer exists"));
            else results.push_back(result(planned.path, DeleteOutcome::Changed, "its folder is now reached through a link"));
        } else if (!(now = snapshotTarget(planned.path, error))) {
            results.push_back(error ? result(planned.path, DeleteOutcome::Failed, error.message())
                                    : result(planned.path, DeleteOutcome::Missing, "it no longer exists"));
        } else if (!(now->identity == planned.identity) || now->kind != planned.kind || now->link != planned.link) {
            results.push_back(result(planned.path, DeleteOutcome::Changed, "it was replaced by a different item"));
        } else if (planned.kind == TargetKind::File && (now->bytes != planned.bytes || now->modified != planned.modified)) {
            results.push_back(result(planned.path, DeleteOutcome::Changed, "it was modified"));
        } else {
            try {
                auto moved = backend.moveToTrash(planned);
                moved.path = planned.path;
                results.push_back(std::move(moved));
            } catch (const std::exception& failure) {
                results.push_back(result(planned.path, DeleteOutcome::Failed, failure.what()));
            } catch (...) {
                results.push_back(result(planned.path, DeleteOutcome::Failed, "unexpected error"));
            }
        }
        if (progress) progress(results.size(), total);
    }
    return results;
}
}
