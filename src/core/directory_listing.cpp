#include "glancelot/directory_listing.hpp"

// Contract stub (lead freeze). FILE-GRID-01 implementation replaces this body.
namespace glancelot {
DirectoryListing listDirectory(const std::filesystem::path&, const SortSpec&, bool, std::stop_token cancel, std::size_t) {
    DirectoryListing result;
    result.cancelled = cancel.stop_requested();
    return result;
}
}
