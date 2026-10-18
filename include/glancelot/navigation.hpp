#pragma once
#include "contracts.hpp"
#include <optional>
#include <system_error>
#include <stop_token>

namespace glancelot {
bool naturalLess(const std::filesystem::path& a, const std::filesystem::path& b);
bool isSupportedImage(const std::filesystem::path& path);
std::vector<std::filesystem::path> imageFiles(const std::filesystem::path& directory, std::stop_token cancel = {});
// File order inside one folder. Folder order and cross-folder landing always stay natural (NAV-06).
// Ties break by natural name; entries whose metadata cannot be read sort last in both directions. Shuffle is a
// deterministic permutation of (file name, seed): it does not change when files are added or removed elsewhere.
enum class SortKey { Name, Modified, Size, Shuffle };
struct SortSpec {
    SortKey key{SortKey::Name};
    bool descending{false};
    std::uint64_t seed{1};
    bool operator==(const SortSpec&) const = default;
};
struct FileMeta { std::int64_t modified{}; std::uintmax_t size{}; bool known{}; };
// The file order itself (exposed so ordering properties, e.g. unreadable metadata last, are testable).
bool fileSortBefore(const SortSpec& sort, const std::filesystem::path& a, const FileMeta& metaA,
                    const std::filesystem::path& b, const FileMeta& metaB);
// `meta`, when given, receives one entry per returned file (same order); only Modified/Size read real metadata.
std::vector<std::filesystem::path> sortedImageFiles(const std::filesystem::path& directory, const SortSpec& sort,
    std::stop_token cancel = {}, std::vector<FileMeta>* meta = nullptr);
// Immediate child directories for the folder tree, in the same natural order and with the same link/volume
// policy as traversal. `accessible` is false when the directory cannot be listed (denied, missing, or reached
// through a link); `cancelled` results carry no folders. Only one directory level is read.
// `truncated` means more than `limit` folders exist; the returned folders are then an unordered-source subset
// (still naturally sorted) so a huge directory cannot make the worker allocate without bound.
struct FolderListing { std::vector<std::filesystem::path> folders; bool accessible{true}, cancelled{false}, truncated{false}; };
FolderListing childFolders(const std::filesystem::path& directory, std::stop_token cancel = {}, std::size_t limit = 20000);
// First eligible image in `directory` or, failing that, in its subfolders in traversal (preorder, natural) order,
// looking at no more than `maxFolders` directories. `exhausted` means the budget ended the search; `accessible`
// is false when `directory` itself cannot be read; a cancelled search returns nothing.
struct SubtreeImage { std::optional<std::filesystem::path> image; bool exhausted{false}, accessible{true}; };
SubtreeImage firstImageInSubtree(const std::filesystem::path& directory, std::stop_token cancel = {}, std::size_t maxFolders = 500);
// Lazy traversal searches directories under root; never follows directory links.
// Cancellation is cooperative; a blocked operating-system filesystem call cannot be interrupted.
std::optional<std::filesystem::path> adjacentFolderImage(
    const std::filesystem::path& root, const std::filesystem::path& directory, int direction,
    std::stop_token cancel = {});
class Navigator {
public:
    bool open(const std::filesystem::path& file, std::error_code& error);
    bool setRoot(const std::filesystem::path& root);
    void setBoundary(BoundaryMode mode);
    // Re-lists the current folder in the new order; the current image stays current.
    void setSort(const SortSpec& sort);
    const SortSpec& sort() const { return sort_; }
    void setCancellation(std::stop_token cancel) { cancel_ = cancel; }
    std::optional<std::filesystem::path> step(int direction);
    // Explicit folder jump: lands on the natural first/last image of the target folder (NAV-02).
    std::optional<std::filesystem::path> folder(int direction);
    void refresh();
    const NavigationSnapshot& snapshot() const { return state_; }
private:
    std::optional<std::filesystem::path> enterFolder(int direction, bool sortedLanding);
    void list(const std::filesystem::path& directory, std::vector<std::filesystem::path>& files, std::vector<FileMeta>& meta);
    NavigationSnapshot state_;
    SortSpec sort_;
    std::vector<FileMeta> meta_;   // parallel to state_.files
    FileMeta anchor_;              // sort key of `current`, kept so a deleted current still has an order position
    std::stop_token cancel_;
};
}
