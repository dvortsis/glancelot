#pragma once
#include "contracts.hpp"
#include <optional>
#include <system_error>
#include <stop_token>

namespace glancelot {
bool naturalLess(const std::filesystem::path& a, const std::filesystem::path& b);
bool isSupportedImage(const std::filesystem::path& path);
std::vector<std::filesystem::path> imageFiles(const std::filesystem::path& directory, std::stop_token cancel = {});
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
    void setCancellation(std::stop_token cancel) { cancel_ = cancel; }
    std::optional<std::filesystem::path> step(int direction);
    std::optional<std::filesystem::path> folder(int direction);
    void refresh();
    const NavigationSnapshot& snapshot() const { return state_; }
private:
    NavigationSnapshot state_;
    std::stop_token cancel_;
};
}
