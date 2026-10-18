#include "glancelot/directory_listing.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// Generated, disposable fixtures only. Link and permission cases that the platform or account cannot create
// are reported as UNVERIFIED instead of failing.
namespace fs = std::filesystem;
using namespace glancelot;
namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}
void write(const fs::path& path, std::size_t bytes = 1) { std::ofstream(path, std::ios::binary) << std::string(bytes, 'x'); }
std::string names(const DirectoryListing& listing) {
    std::string out;
    for (const auto& entry : listing.entries) out += (out.empty() ? "" : ",") + entry.path.filename().string();
    return out;
}
const DirectoryEntry* find(const DirectoryListing& listing, const std::string& name) {
    for (const auto& entry : listing.entries) if (entry.path.filename() == name) return &entry;
    return nullptr;
}
struct Fixture {
    fs::path root;
    Fixture() {
        std::random_device random;
        root = fs::temp_directory_path() / ("glancelot-listing-" + std::to_string(random()));
        fs::create_directories(root);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    fs::path dir(const std::string& name) { auto p = root / name; fs::create_directories(p); return p; }
};
#ifdef _WIN32
int run(const std::string& command) { return std::system(command.c_str()); }
#endif
}

int main() {
    Fixture fx;
    {   // Folders first in natural order, then every regular file; images and others share the file order.
        const auto d = fx.dir("mixed");
        for (const char* folder : {"b10", "b2", "a"}) fs::create_directory(d / folder);
        for (const char* file : {"img10.png", "img2.png", "notes.txt", "README", "Upper.PNG", "photo.jpeg", "archive.tar.gz"}) write(d / file);
        const auto listing = listDirectory(d, {});
        check(listing.accessible && !listing.cancelled && !listing.truncated, "mixed listing flags");
        check(names(listing) == "a,b2,b10,archive.tar.gz,img2.png,img10.png,notes.txt,photo.jpeg,README,Upper.PNG", "folders-first natural order: " + names(listing));
        check(find(listing, "a") && find(listing, "a")->kind == EntryKind::Folder, "folder kind");
        check(find(listing, "img2.png")->kind == EntryKind::Image && find(listing, "Upper.PNG")->kind == EntryKind::Image
              && find(listing, "photo.jpeg")->kind == EntryKind::Image, "supported images");
        check(find(listing, "notes.txt")->kind == EntryKind::File && find(listing, "README")->kind == EntryKind::File
              && find(listing, "archive.tar.gz")->kind == EntryKind::File, "unsupported files are File");
        for (const auto& entry : listing.entries) check(!entry.link && !entry.hidden, "plain entries carry no link/hidden flag");
        // Name descending reverses files only; folders keep natural ascending order.
        const auto reversed = listDirectory(d, SortSpec{SortKey::Name, true});
        check(names(reversed) == "a,b2,b10,Upper.PNG,README,photo.jpeg,notes.txt,img10.png,img2.png,archive.tar.gz", "name descending: " + names(reversed));
        std::cout << "ordering and types ok\n";
    }
    {   // Empty folder, missing folder, and a file passed as a folder.
        const auto empty = fx.dir("empty");
        const auto listing = listDirectory(empty, {});
        check(listing.accessible && listing.entries.empty() && !listing.truncated, "empty folder is accessible and empty");
        check(!listDirectory(fx.root / "missing", {}).accessible, "missing folder is inaccessible");
        write(fx.root / "plain.txt");
        check(!listDirectory(fx.root / "plain.txt", {}).accessible, "a file is not a listable folder");
        check(!listDirectory(fs::path{}, {}).accessible, "empty path is inaccessible");
        std::cout << "empty/missing ok\n";
    }
    {   // Hidden entries: excluded by default, listed and flagged when requested.
        const auto d = fx.dir("hidden");
        write(d / ".dot.png"); write(d / "visible.png"); fs::create_directory(d / ".config");
        int expectedHidden = 2;
#ifdef _WIN32
        write(d / "attr.txt");
        check(SetFileAttributesW((d / "attr.txt").c_str(), FILE_ATTRIBUTE_HIDDEN) != 0, "set hidden attribute");
        ++expectedHidden;
#endif
        const auto shown = listDirectory(d, {});
        check(names(shown) == "visible.png", "hidden excluded by default: " + names(shown));
        const auto all = listDirectory(d, {}, true);
        int hidden = 0; for (const auto& e : all.entries) hidden += e.hidden;
        check(int(all.entries.size()) == expectedHidden + 1 && hidden == expectedHidden, "includeHidden lists and flags hidden entries");
        check(find(all, ".config") && find(all, ".config")->kind == EntryKind::Folder, "hidden folder still a folder");
        std::cout << "hidden ok\n";
    }
    {   // Links: listed with a badge, never Image, never listed through.
        const auto d = fx.dir("links");
        const auto target = fx.dir("link-target");
        write(target / "inside.png"); write(d / "real.png");
        bool anyLink = false;
#ifdef _WIN32
        const std::string junction = "cmd /d /c mklink /J \"" + (d / "junction").string() + "\" \"" + target.string() + "\" >nul 2>&1";
        if (run(junction) == 0 && fs::exists(d / "junction")) {
            anyLink = true;
            const auto listing = listDirectory(d, {});
            const auto* entry = find(listing, "junction");
            check(entry && entry->kind == EntryKind::Folder && entry->link, "junction is a linked folder");
            check(!listDirectory(d / "junction", {}).accessible, "a junction is never listed through");
            std::cout << "windows junction ok\n";
        } else std::cout << "UNVERIFIED: could not create a junction with mklink /J\n";
#endif
        std::error_code ec;
        fs::create_directory_symlink(target, d / "dirlink", ec);
        if (!ec) {
            anyLink = true;
            const auto listing = listDirectory(d, {});
            const auto* entry = find(listing, "dirlink");
            check(entry && entry->kind == EntryKind::Folder && entry->link, "directory symlink is a linked folder");
            check(!listDirectory(d / "dirlink", {}).accessible, "a directory symlink is never listed through");
            std::cout << "directory symlink ok\n";
        } else std::cout << "UNVERIFIED: directory symlink not creatable here (" << ec.message() << ")\n";
        ec.clear();
        fs::create_symlink(d / "real.png", d / "filelink.png", ec);
        if (!ec) {
            anyLink = true;
            const auto linkedListing = listDirectory(d, {});
            const auto* entry = find(linkedListing, "filelink.png");
            check(entry && entry->kind == EntryKind::File && entry->link, "a linked .png is a File, never an Image");
            ec.clear();
            fs::create_symlink(d / "gone.png", d / "dangling.png", ec);
            if (!ec) {
                const auto danglingListing = listDirectory(d, {});
                const auto* dangling = find(danglingListing, "dangling.png");
                check(dangling && dangling->kind == EntryKind::File && dangling->link, "a dangling link stays visible as a linked File");
            }
            std::cout << "file symlink ok\n";
        } else std::cout << "UNVERIFIED: file symlink not creatable here (" << ec.message() << ")\n";
        check(find(listDirectory(d, {}), "real.png")->kind == EntryKind::Image, "regular image next to links");
        if (!anyLink) std::cout << "UNVERIFIED: no link type could be created on this machine\n";
    }
    {   // Permission denied: inaccessible, not empty.
        const auto d = fx.dir("denied");
        write(d / "secret.png");
#ifdef _WIN32
        const std::string deny = "icacls \"" + d.string() + "\" /deny *S-1-1-0:(RD) >nul 2>&1";
        const std::string restore = "icacls \"" + d.string() + "\" /remove:d *S-1-1-0 >nul 2>&1";
        if (run(deny) == 0) {
            const auto listing = listDirectory(d, {});
            run(restore); // always restore before checks so the fixture can be deleted
            check(!listing.accessible && listing.entries.empty(), "permission-denied folder is inaccessible");
            check(listDirectory(d, {}).accessible, "access restored");
            std::cout << "permission denied ok\n";
        } else std::cout << "UNVERIFIED: icacls deny failed\n";
#else
        if (::geteuid() == 0) std::cout << "UNVERIFIED: running as root; permission checks are bypassed\n";
        else {
            ::chmod(d.c_str(), 0);
            const auto listing = listDirectory(d, {});
            ::chmod(d.c_str(), 0755);
            check(!listing.accessible && listing.entries.empty(), "permission-denied folder is inaccessible");
            std::cout << "permission denied ok\n";
        }
#endif
    }
    {   // Cancellation: before the call, and racing a large listing. A cancelled result never carries entries.
        const auto d = fx.dir("large");
        for (int i = 0; i < 20000; ++i) write(d / ("f" + std::to_string(i) + (i % 3 ? ".png" : ".bin")), 0);
        std::stop_source stopped; stopped.request_stop();
        const auto pre = listDirectory(d, {}, false, stopped.get_token());
        check(pre.cancelled && pre.entries.empty() && !pre.truncated, "pre-cancelled listing is empty");
        int cancelledRuns = 0;
        for (int attempt = 0; attempt < 5; ++attempt) {
            std::stop_source stop;
            std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(attempt * 3)); stop.request_stop(); });
            const auto listing = listDirectory(d, SortSpec{SortKey::Size, false}, false, stop.get_token());
            canceller.join();
            if (listing.cancelled) { ++cancelledRuns; check(listing.entries.empty() && !listing.truncated, "racing cancel carries no entries"); }
            else check(listing.entries.size() == 20000, "uncancelled race completes fully");
        }
        const auto start = std::chrono::steady_clock::now();
        const auto full = listDirectory(d, {});
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(full.entries.size() == 20000 && !full.truncated, "20000-entry listing complete");
        std::cout << "cancellation ok (" << cancelledRuns << "/5 racing runs cancelled); 20000 entries listed in " << ms << " ms\n";
        // Truncation: a bounded subset, still ordered, flagged.
        const auto bounded = listDirectory(d, {}, false, {}, 100);
        check(bounded.truncated && bounded.entries.size() == 100, "truncated at the limit");
        bool ordered = true;
        for (std::size_t i = 1; i < bounded.entries.size(); ++i) ordered = ordered && !naturalLess(bounded.entries[i].path, bounded.entries[i - 1].path);
        check(ordered, "truncated subset is still sorted");
    }
    {   // Exact limit is not truncation; folders count toward the limit.
        const auto d = fx.dir("limit");
        fs::create_directory(d / "folder"); write(d / "a.png"); write(d / "b.txt");
        check(!listDirectory(d, {}, false, {}, 3).truncated && listDirectory(d, {}, false, {}, 3).entries.size() == 3, "limit equal to count");
        const auto two = listDirectory(d, {}, false, {}, 2);
        check(two.truncated && two.entries.size() == 2, "limit below count");
        check(listDirectory(d, {}, false, {}, 0).truncated && listDirectory(d, {}, false, {}, 0).entries.empty(), "zero limit");
        std::cout << "truncation ok\n";
    }
    {   // Sort keys order files only; folders stay natural and first.
        const auto d = fx.dir("sorted");
        fs::create_directory(d / "z-folder"); fs::create_directory(d / "a-folder");
        write(d / "big.txt", 300); write(d / "small.png", 10); write(d / "mid.png", 200);
        const auto now = fs::file_time_type::clock::now();
        fs::last_write_time(d / "big.txt", now - std::chrono::hours(3));
        fs::last_write_time(d / "small.png", now - std::chrono::hours(1));
        fs::last_write_time(d / "mid.png", now - std::chrono::hours(2));
        check(names(listDirectory(d, SortSpec{SortKey::Size, false})) == "a-folder,z-folder,small.png,mid.png,big.txt", "size ascending");
        check(names(listDirectory(d, SortSpec{SortKey::Size, true})) == "a-folder,z-folder,big.txt,mid.png,small.png", "size descending");
        check(names(listDirectory(d, SortSpec{SortKey::Modified, false})) == "a-folder,z-folder,big.txt,mid.png,small.png", "modified ascending: " + names(listDirectory(d, SortSpec{SortKey::Modified, false})));
        check(names(listDirectory(d, SortSpec{SortKey::Modified, true})) == "a-folder,z-folder,small.png,mid.png,big.txt", "modified descending");
        const auto shuffled = names(listDirectory(d, SortSpec{SortKey::Shuffle, false, 7}));
        check(shuffled == names(listDirectory(d, SortSpec{SortKey::Shuffle, false, 7})), "shuffle is deterministic for a seed");
        check(shuffled.rfind("a-folder,z-folder,", 0) == 0, "shuffle keeps folders first and natural");
        const auto listing = listDirectory(d, SortSpec{SortKey::Size, false});
        check(find(listing, "big.txt")->meta.known && find(listing, "big.txt")->meta.size == 300, "size metadata");
        std::cout << "sort keys ok\n";
    }
    {   // REVIEW (adversarial): an external delete burst racing the listing must never fail the folder closed.
        const auto d = fx.dir("burst");
        for (int i = 0; i < 8000; ++i) write(d / ("del" + std::to_string(i) + ".png"), 0);
        std::atomic<bool> done{false};
        std::thread deleter([&] { std::error_code ec; for (int i = 0; i < 8000; ++i) fs::remove(d / ("del" + std::to_string(i) + ".png"), ec); done = true; });
        int runs = 0, inaccessible = 0;
        while (!done || runs == 0) { const auto l = listDirectory(d, {}); ++runs; if (!l.accessible) ++inaccessible; }
        deleter.join();
        check(inaccessible == 0, "delete burst flipped a live folder inaccessible " + std::to_string(inaccessible) + "/" + std::to_string(runs));
        std::cout << "delete burst ok (" << runs << " listings, " << inaccessible << " inaccessible)\n";
        // Same, with creation and deletion interleaved (sync-tool churn).
        std::atomic<bool> stop{false};
        std::thread churn([&] { std::error_code ec; for (int round = 0; round < 20; ++round) {
            for (int i = 0; i < 400; ++i) write(d / ("c" + std::to_string(i) + ".txt"));
            for (int i = 0; i < 400; ++i) fs::remove(d / ("c" + std::to_string(i) + ".txt"), ec); } stop = true; });
        runs = 0; inaccessible = 0;
        while (!stop) { const auto l = listDirectory(d, {}); ++runs; if (!l.accessible) ++inaccessible; }
        churn.join();
        check(inaccessible == 0, "create/delete churn flipped a live folder inaccessible " + std::to_string(inaccessible) + "/" + std::to_string(runs));
        std::cout << "churn ok (" << runs << " listings, " << inaccessible << " inaccessible)\n";
    }
    if (failures) { std::cerr << failures << " directory listing check(s) failed\n"; return 1; }
    std::cout << "directory listing tests passed\n";
    return 0;
}
