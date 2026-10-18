#include "glancelot/directory_listing.hpp"
#include <iostream>

// Contract placeholder; FILE-GRID-01 adds generated-fixture listing tests here.
int main() {
    std::stop_source stop; stop.request_stop();
    const auto listing = glancelot::listDirectory(std::filesystem::temp_directory_path(), {}, false, stop.get_token());
    if (!listing.cancelled || !listing.entries.empty()) { std::cerr << "cancelled listing must be empty\n"; return 1; }
    std::cout << "directory listing contract ok\n";
    return 0;
}
