#include "glancelot/navigation.hpp"
#include <algorithm>
#include <chrono>
#include <atomic>
#include <thread>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace glancelot;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    fs::path root;
    Fixture() {
        root = fs::temp_directory_path() / ("glancelot-navigation-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    fs::path file(const fs::path& name) {
        const auto p = root / name;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << "generated navigation fixture";
        return p;
    }
};
void naturalOrdering() {
    // ASCII case folding only; native non-ASCII units remain ordinal. Equal numeric
    // values (including leading zeros) use raw names as the final deterministic tie.
    std::vector<fs::path> names{"img10.jpg", "img2.jpg", "img01.jpg", "IMG1.jpg", "img1.jpg", "img001.jpg"};
    std::sort(names.begin(), names.end(), naturalLess);
    const std::vector<fs::path> expected{"IMG1.jpg", "img001.jpg", "img01.jpg", "img1.jpg", "img2.jpg", "img10.jpg"};
    require(names == expected, "natural numeric/case/zero ties differ");
    require(naturalLess("x01a.jpg", "x1b.jpg"), "zero run must not break suffix comparison");
    require(naturalLess("x000.jpg", "x1.jpg"), "all-zero run failed");
    const auto huge = std::string(300, '9');
    require(naturalLess(fs::path("x" + huge + ".png"), fs::path("x1" + std::string(300, '0') + ".png")), "numeric runs overflowed");
    require(!naturalLess("same.png", "same.png"), "comparator is not irreflexive");
    for (const auto& a : names) for (const auto& b : names) for (const auto& c : names) {
        require(!(naturalLess(a, b) && naturalLess(b, a)), "comparator is asymmetric");
        if (naturalLess(a, b) && naturalLess(b, c)) require(naturalLess(a, c), "comparator is not transitive");
    }
    require(isSupportedImage("photo.JPEG") && isSupportedImage("photo.PnG"), "extension case handling");
    require(!isSupportedImage("photo.gif") && !isSupportedImage("photo.png.txt"), "unqualified extension supported");
}
void traversal() {
    Fixture f;
    const auto a = f.file("a.jpg"), b = f.file("b.png");
    const auto c = f.file("01-trip/c.jpg");
    const auto d = f.file("01-trip/01-details/d.jpg");
    fs::create_directories(f.root / "02-empty/deep/empty");
    f.file("02-empty/readme.txt");
    const auto e = f.file("10-family/e.jpeg"), z = f.file("10-family/f.jpg");
    const std::vector<fs::path> expected{a, b, c, d, e, z};
    Navigator nav;
    std::error_code ec;
    require(nav.open(a, ec) && !ec, "open root image");
    std::vector<fs::path> actual{a};
    for (int i = 0; i < 20; ++i) {
        const auto next = nav.step(1);
        if (!next) break;
        actual.push_back(*next);
    }
    require(actual == expected, "preorder forward sequence differs");
    require(!nav.step(1), "root must not wrap");
    actual = {z};
    for (int i = 0; i < 20; ++i) {
        const auto previous = nav.step(-1);
        if (!previous) break;
        actual.push_back(*previous);
    }
    auto reverse = expected;
    std::reverse(reverse.begin(), reverse.end());
    require(actual == reverse, "reverse traversal is not exact inverse");
    require(!nav.step(-1), "start must not escape root");
    require(nav.folder(1) == c, "next folder first image");
    require(nav.folder(1) == d, "next nested folder");
    require(nav.folder(1) == e, "empty subtree bypass");
    require(nav.folder(-1) == d, "previous folder deepest subtree");
    require(nav.folder(-1) == c, "previous folder parent directory");
    require(nav.folder(-1) == b, "previous folder last image");
    require(!nav.folder(-1), "explicit previous folder escapes root");
    require(!nav.folder(0) && !nav.step(0), "zero direction must be no-op");
}
void rootAndBoundaries() {
    Fixture f;
    const auto first = f.file("1/a.jpg"), last = f.file("1/b.jpg"), next = f.file("2/c.png");
    Navigator nav;
    std::error_code ec;
    require(nav.open(first, ec), "open child");
    require(nav.snapshot().root == first.parent_path(), "shell open must bound root to containing dir");
    require(nav.step(1) == last && !nav.step(1), "default root should not scan siblings");
    nav.setBoundary(BoundaryMode::Stop);
    require(!nav.step(1), "stop failed");
    nav.setBoundary(BoundaryMode::LoopFolder);
    require(nav.step(1) == first && nav.step(-1) == last, "folder wrap failed");
    require(nav.setRoot(f.root), "ancestor expansion rejected");
    require(!nav.setRoot(f.root / "2"), "unrelated root accepted");
    require(!nav.setRoot(f.root / "1"), "root contraction accepted");
    nav.setBoundary(BoundaryMode::ContinueTree);
    require(nav.step(1) == next, "expanded root must expose sibling");
    const auto before = nav.snapshot().current;
    require(!nav.open(f.root / "does-not-exist.jpg", ec) && ec, "missing input accepted");
    require(nav.snapshot().current == before, "failed open changed current");
    require(!nav.open(f.file("unsupported.gif"), ec) && ec, "unsupported input accepted");
    require(!nav.setRoot({}), "empty root accepted");
#ifdef _WIN32
    require(nav.open(f.root / "1/A.JPG", ec), "Windows case-insensitive shell path rejected");
    require(nav.snapshot().current.filename() == "a.jpg", "current must use listed native name");
#endif
}
void filesystemMutation() {
    Fixture f;
    const auto a = f.file("a.jpg"), b = f.file("b.jpg"), c = f.file("c.jpg");
    Navigator nav;
    std::error_code ec;
    require(nav.open(b, ec), "open mutation anchor");
    fs::remove(b);
    nav.refresh();
    require(nav.snapshot().files == std::vector<fs::path>{a, c}, "deleted file retained in listing");
    require(nav.snapshot().current == b, "deleted anchor lost");
    require(nav.step(1) == c, "deleted anchor forward failed");
    require(nav.open(c, ec), "reopen surviving image");
    fs::remove(c);
    require(nav.step(-1) == a, "deleted anchor reverse failed");
    const auto child = f.file("01-old/p.png"), sibling = f.file("02-next/q.png");
    require(nav.open(child, ec) && nav.setRoot(f.root), "deleted directory setup");
    fs::remove_all(child.parent_path());
    require(nav.folder(1) == sibling, "deleted directory forward anchor failed");
    fs::remove_all(sibling.parent_path());
    require(nav.folder(-1) == a, "deleted directory reverse anchor failed");
    require(!adjacentFolderImage(f.root / "01-old", f.root, 1), "direct helper escaped root");
}
void generatedOracle() {
    Fixture f;
    std::vector<fs::path> expected;
    // Independent fixture construction supplies preorder numerically; it does not
    // call the production sorting/traversal helpers to derive expected results.
    const auto start = f.file("start.jpg");
    expected.push_back(start);
    for (int i = 1; i <= 25; ++i) {
        const auto directory = fs::path("folder" + std::to_string(i));
        fs::create_directories(f.root / directory);
        if (i % 4 != 0) {
            expected.push_back(f.file(directory / "image2.jpg"));
            expected.push_back(f.file(directory / "image10.png"));
        }
        if (i % 3 == 0) expected.push_back(f.file(directory / "child9/deeper.jpg"));
    }
    Navigator nav;
    std::error_code ec;
    require(nav.open(start, ec), "oracle open");
    for (std::size_t i = 1; i < expected.size(); ++i)
        require(nav.step(1) == expected[i], "generated forward oracle mismatch");
    require(!nav.step(1), "generated end boundary");
    for (std::size_t i = expected.size() - 1; i > 0; --i)
        require(nav.step(-1) == expected[i - 1], "generated reverse oracle mismatch");
    require(!nav.step(-1), "generated start boundary");
}
void permissionBypass() {
#ifndef _WIN32
    Fixture f;
    f.file("a.jpg");
    const auto hidden = f.file("01-denied/x.jpg");
    const auto visible = f.file("02-visible/y.jpg");
    fs::permissions(hidden.parent_path(), fs::perms::none);
    const auto destination = adjacentFolderImage(f.root, f.root, 1);
    // Root/superuser may bypass Unix mode restrictions; report that explicitly.
    const bool denied = imageFiles(hidden.parent_path()).empty();
    fs::permissions(hidden.parent_path(), fs::perms::owner_all);
    require(destination == (denied ? visible : hidden), "permission denied directory did not bypass");
    std::cout << "permission-denial enforcement=" << denied << '\n';
#endif
}
void cancellation() {
    Fixture f;
    const auto a = f.file("a.jpg"), b = f.file("b.jpg");
    f.file("child/c.jpg");
    Navigator nav;
    std::error_code ec;
    require(nav.open(a, ec), "cancellation setup");
    const auto before = nav.snapshot();
    std::stop_source stopped;
    stopped.request_stop();
    const auto token = stopped.get_token();
    require(imageFiles(f.root, token).empty(), "stopped listing returned entries");
    require(!adjacentFolderImage(f.root, f.root, 1, token), "stopped traversal returned target");
    nav.setCancellation(token);
    require(!nav.open(b, ec) && ec == std::errc::operation_canceled, "stopped open error");
    require(!nav.step(1) && !nav.folder(1) && !nav.setRoot(f.root), "stopped navigator returned target");
    nav.refresh();
    require(nav.snapshot().current == before.current && nav.snapshot().files == before.files
        && nav.snapshot().generation == before.generation, "already-stopped operation mutated snapshot");
    nav.setCancellation({});
    require(nav.step(1) == b, "new uncancelled token did not resume navigation");

    // A wide empty tree would require repeated sibling scans without cancellation.
    // Request stop from another thread after entering the worker operation. The
    // test reports if an exceptionally fast filesystem completes before the stop.
    Fixture wide;
    const auto start = wide.file("start.jpg");
    for (int i = 0; i < 500; ++i)
        fs::create_directories(wide.root / ("folder" + std::to_string(i)));
    wide.file("folder9999/end.jpg");
    Navigator scanning;
    require(scanning.open(start, ec), "mid-scan setup");
    const auto stable = scanning.snapshot();
    std::stop_source running;
    scanning.setCancellation(running.get_token());
    std::atomic<bool> entered{false};
    std::jthread stopper([&] {
        while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        running.request_stop();
    });
    entered.store(true, std::memory_order_release);
    const auto result = scanning.folder(1);
    const bool interrupted = !result && running.stop_requested();
    stopper.join();
    if (interrupted) require(scanning.snapshot().current == stable.current
        && scanning.snapshot().files == stable.files && scanning.snapshot().generation == stable.generation,
        "cancelled in-progress folder navigation mutated snapshot");
    std::cout << "in-progress cancellation observed=" << interrupted << '\n';
}
void linksAndLaziness() {
    Fixture f, outside;
    const auto a = f.file("a.jpg"), b = f.file("10-real/b.jpg");
    outside.file("secret.jpg");
    std::error_code ec;
    fs::create_directory_symlink(outside.root, f.root / "00-link", ec);
    const bool directoryLinkAvailable = !ec;
    ec.clear();
    fs::create_directory_symlink(f.root, f.root / "10-real/cycle", ec);
    const bool cycleAvailable = !ec;
    ec.clear();
    fs::create_symlink(b, f.root / "alias.jpg", ec);
    const bool fileLinkAvailable = !ec;
    require(imageFiles(f.root) == std::vector<fs::path>{a}, "file link included in listing");
    require(adjacentFolderImage(f.root, f.root, 1) == b, "directory link followed");
    require(!adjacentFolderImage(f.root, b.parent_path(), 1), "cycle/root termination failed");
    if (directoryLinkAvailable) require(!adjacentFolderImage(f.root, f.root / "00-link", 1), "direct linked directory followed");
    // Changes after the first search are visible: no eager persistent full-tree snapshot.
    const auto fresh = f.file("02-fresh/c.jpg");
    require(adjacentFolderImage(f.root, f.root, 1) == fresh, "folder traversal cached an eager tree");
    std::cout << "link creation: directory=" << directoryLinkAvailable << " cycle=" << cycleAvailable
              << " file=" << fileLinkAvailable << '\n';
}
}
void childFoldersAndSubtreeSearch() {
    Fixture f;
    for (const char* name : {"10", "2", "a/deep/deeper", "B", "1"}) fs::create_directories(f.root / name);
    f.file("file.jpg");
    auto names = [](const FolderListing& listing) {
        std::vector<std::string> out;
        for (const auto& folder : listing.folders) out.push_back(folder.filename().string());
        return out;
    };
    const auto listing = childFolders(f.root);
    require(listing.accessible && !listing.cancelled && !listing.truncated, "child listing flags");
    require(names(listing) == std::vector<std::string>({"1", "2", "10", "a", "B"}), "child folders: natural order, one level, files ignored");
    std::stop_source stopped; stopped.request_stop();
    const auto cancelled = childFolders(f.root, stopped.get_token());
    require(cancelled.cancelled && cancelled.folders.empty(), "cancelled child listing returned folders");
    require(!childFolders(f.root / "missing").accessible && !childFolders(f.root / "file.jpg").accessible, "missing or non-directory listed as accessible");
    require(childFolders(f.root / "1").accessible && childFolders(f.root / "1").folders.empty(), "empty folder should list as accessible and empty");
    Fixture many;
    for (int i = 0; i < 30; ++i) fs::create_directories(many.root / ("d" + std::to_string(100 + i)));
    const auto limited = childFolders(many.root, {}, 10);
    require(limited.truncated && limited.folders.size() == 10, "limit did not bound the listing");
    require(std::is_sorted(limited.folders.begin(), limited.folders.end(), naturalLess), "truncated listing is not sorted");
    require(!childFolders(many.root, {}, 30).truncated, "exact limit must not report truncation");
    std::error_code ec;
    Fixture outside; fs::create_directories(outside.root / "target");
    fs::create_directory_symlink(outside.root, f.root / "00-link", ec);
    if (!ec) {
        require(names(childFolders(f.root)).front() != "00-link", "linked directory listed in tree");
        require(!childFolders(f.root / "00-link").accessible, "linked directory listed directly");
    }
#ifndef _WIN32
    fs::create_directories(f.root / "locked");
    fs::permissions(f.root / "locked", fs::perms::none);
    const bool denied = !childFolders(f.root / "locked").accessible;
    fs::permissions(f.root / "locked", fs::perms::owner_all);
    std::cout << "child-folder permission-denial enforcement=" << denied << '\n';
#endif

#ifndef _WIN32
    struct stat rootDevice {}, procDevice {};
    if (::stat("/", &rootDevice)==0 && ::stat("/proc", &procDevice)==0 && rootDevice.st_dev!=procDevice.st_dev) {
        const auto automatic=childFolders("/");
        const auto explicitBrowser=childFolders("/",{},20000,true);
        auto hasProc=[](const FolderListing& result){return std::find(result.folders.begin(),result.folders.end(),fs::path("/proc"))!=result.folders.end();};
        require(!hasProc(automatic),"automatic traversal crossed /proc mount boundary");
        require(hasProc(explicitBrowser),"explicit folder browser omitted a mounted location");
        std::cout << "explicit browser mount opt-in verified=1\n";
    }
#endif

    // Subtree search: preorder (own files, then children in natural order), budgeted and cancellable.
    Fixture t;
    fs::create_directories(t.root / "a/empty");
    const auto first = t.file("a/b/img1.jpg");
    t.file("c/img0.jpg");
    const auto found = firstImageInSubtree(t.root);
    require(found.image == first && !found.exhausted && found.accessible, "subtree preorder first image");
    const auto own = t.file("top.png");
    require(firstImageInSubtree(t.root).image == own, "a folder own images win over subfolders");
    fs::remove(own);
    const auto tight = firstImageInSubtree(t.root, {}, 2);
    require(!tight.image && tight.exhausted, "budget did not stop the search");
    require(!firstImageInSubtree(t.root, stopped.get_token()).image, "cancelled search returned an image");
    require(!firstImageInSubtree(t.root / "missing").accessible, "missing subtree root accessible");
    Fixture wide;
    for (int i = 0; i < 600; ++i) fs::create_directories(wide.root / ("f" + std::to_string(1000 + i)));
    const auto end = wide.file("zz/end.jpg");
    const auto bounded = firstImageInSubtree(wide.root);
    require(!bounded.image && bounded.exhausted, "default 500-folder budget not enforced");
    require(firstImageInSubtree(wide.root, {}, 1000).image == end, "larger budget should find the image");
    // A tight pending-work budget must keep the preorder prefix, including deep children
    // ahead of queued ancestor siblings, and report unexplored branches honestly.
    Fixture prefix;
    fs::create_directories(prefix.root/"a/aa");
    const auto prefixImage=prefix.file("a/aa/first.jpg");prefix.file("b/later.jpg");
    require(firstImageInSubtree(prefix.root,{},3).image==prefixImage,"bounded pending stack changed preorder prefix");
    fs::remove(prefixImage);
    const auto pruned=firstImageInSubtree(prefix.root,{},3);
    require(!pruned.image&&pruned.exhausted,"pruned sibling not reported as budget exhaustion");
    Fixture bare; fs::create_directories(bare.root / "x/y");
    const auto none = firstImageInSubtree(bare.root);
    require(!none.image && !none.exhausted && none.accessible, "image-less subtree misreported");
}
void fileSorting() {
    Fixture f;
    // name, bytes, minutes after a fixed epoch
    struct Spec { const char* name; int bytes; int minutes; };
    const Spec specs[]{{"b.jpg", 30, 5}, {"a10.jpg", 10, 20}, {"a2.png", 30, 10}, {"c.jpeg", 20, 5}, {"d.jpg", 5, 40}};
    const auto epoch = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& spec : specs) {
        const auto path = f.root / spec.name;
        std::ofstream(path, std::ios::binary) << std::string(std::size_t(spec.bytes), 'x');
        fs::last_write_time(path, epoch + std::chrono::minutes(spec.minutes));
    }
    const auto names = [](const std::vector<fs::path>& files) {
        std::string joined; for (const auto& p : files) joined += p.filename().string() + " "; return joined;
    };
    const auto order = [&](SortKey key, bool descending, std::uint64_t seed = 1) {
        return names(sortedImageFiles(f.root, SortSpec{key, descending, seed}));
    };
    require(order(SortKey::Name, false) == "a2.png a10.jpg b.jpg c.jpeg d.jpg ", "name ascending");
    require(order(SortKey::Name, true) == "d.jpg c.jpeg b.jpg a10.jpg a2.png ", "name descending");
    require(order(SortKey::Size, false) == "d.jpg a10.jpg c.jpeg a2.png b.jpg ", "size ascending, ties natural");
    require(order(SortKey::Size, true) == "a2.png b.jpg c.jpeg a10.jpg d.jpg ", "size descending keeps natural ties");
    require(order(SortKey::Modified, false) == "b.jpg c.jpeg a2.png a10.jpg d.jpg ", "modified ascending, ties natural");
    require(order(SortKey::Modified, true) == "d.jpg a10.jpg a2.png b.jpg c.jpeg ", "modified descending keeps natural ties");
    const auto shuffled = order(SortKey::Shuffle, false, 7);
    require(shuffled == order(SortKey::Shuffle, false, 7), "shuffle is not deterministic");
    std::string reversedAll = names([&] { auto v = sortedImageFiles(f.root, SortSpec{SortKey::Shuffle, true, 7}); std::reverse(v.begin(), v.end()); return v; }());
    require(reversedAll == shuffled, ("descending shuffle is not the reverse permutation: " + reversedAll + "| " + shuffled).c_str());
    bool differs = false;
    for (std::uint64_t seed = 1; seed < 40 && !differs; ++seed) differs = order(SortKey::Shuffle, false, seed) != shuffled;
    require(differs, "shuffle seed has no effect");
    auto sorted = sortedImageFiles(f.root, SortSpec{SortKey::Shuffle, false, 7});
    std::sort(sorted.begin(), sorted.end(), naturalLess);
    require(names(sorted) == order(SortKey::Name, false), "shuffle is not a permutation");
    f.file("zz-new.jpg");
    const auto grown = order(SortKey::Shuffle, false, 7);
    std::string without; { std::istringstream in(grown); std::string token; while (in >> token) if (token != "zz-new.jpg") without += token + " "; }
    require(without == shuffled, "adding a file reordered unrelated shuffle entries");
    fs::remove(f.root / "zz-new.jpg");
    std::stop_source stop; stop.request_stop();
    require(sortedImageFiles(f.root, SortSpec{SortKey::Size, false, 1}, stop.get_token()).empty(), "cancelled sort returned files");
    std::vector<FileMeta> meta;
    const auto bySize = sortedImageFiles(f.root, SortSpec{SortKey::Size, false, 1}, {}, &meta);
    require(meta.size() == bySize.size() && meta.front().known && meta.front().size == 5, "metadata is not parallel to files");

    // Navigation follows the active sort, keeps the current image when the sort changes, and survives deleting it.
    Navigator nav; std::error_code error;
    require(nav.open(f.root / "a10.jpg", error), "open for sorted navigation");
    nav.setSort(SortSpec{SortKey::Size, false, 1});
    require(nav.snapshot().current.filename() == "a10.jpg", "sort change lost the current image");
    require(names(nav.snapshot().files) == "d.jpg a10.jpg c.jpeg a2.png b.jpg ", "snapshot not in sort order");
    auto next = nav.step(1);
    require(next && next->filename() == "c.jpeg", "next in size order");
    require(nav.step(-1)->filename() == "a10.jpg" && nav.step(-1)->filename() == "d.jpg", "previous in size order");
    require(nav.step(1)->filename() == "a10.jpg" && nav.step(1)->filename() == "c.jpeg", "walk back to c.jpeg");
    fs::remove(f.root / "c.jpeg");
    next = nav.step(1);
    require(next && next->filename() == "a2.png", "deleted current: next did not use its sort position");
    fs::remove(f.root / "a2.png");
    next = nav.step(-1);
    require(next && next->filename() == "a10.jpg", "deleted current: previous did not use its sort position");
    nav.setSort(SortSpec{SortKey::Name, true, 1});
    require(names(nav.snapshot().files) == "d.jpg b.jpg a10.jpg " && nav.snapshot().current.filename() == "a10.jpg", "name descending after deletes");

    // Unreadable metadata sorts last in both directions and the comparator stays a strict weak order.
    {
        const FileMeta known{100, 5, true}, other{200, 9, true}, unknown{};
        for (const auto key : {SortKey::Modified, SortKey::Size}) for (const bool desc : {false, true}) {
            const SortSpec spec{key, desc, 1};
            require(fileSortBefore(spec, "z.jpg", known, "a.jpg", unknown), "readable metadata must precede unreadable");
            require(!fileSortBefore(spec, "a.jpg", unknown, "z.jpg", known), "unreadable metadata must be last");
            require(fileSortBefore(spec, "a.jpg", unknown, "b.jpg", unknown) && !fileSortBefore(spec, "b.jpg", unknown, "a.jpg", unknown), "unreadable ties are natural");
            const std::pair<fs::path, FileMeta> items[]{{"a.jpg", known}, {"b.jpg", other}, {"c.jpg", unknown}, {"d.jpg", known}, {"e.jpg", unknown}};
            for (const auto& x : items) {
                require(!fileSortBefore(spec, x.first, x.second, x.first, x.second), "comparator not irreflexive");
                for (const auto& y : items) for (const auto& z : items) {
                    if (fileSortBefore(spec, x.first, x.second, y.first, y.second)) require(!fileSortBefore(spec, y.first, y.second, x.first, x.second), "comparator not asymmetric");
                    if (fileSortBefore(spec, x.first, x.second, y.first, y.second) && fileSortBefore(spec, y.first, y.second, z.first, z.second))
                        require(fileSortBefore(spec, x.first, x.second, z.first, z.second), "comparator not transitive");
                }
            }
        }
    }

    // Stepping across a folder boundary continues the visible order (nothing skipped); explicit jumps stay natural.
    Fixture w;
    for (const char* folder : {"a", "b"}) for (const auto& [name, bytes] : {std::pair{"1.jpg", 30}, std::pair{"2.jpg", 20}, std::pair{"3.jpg", 10}}) {
        const auto path = w.root / folder / name; fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << std::string(std::size_t(bytes), 'x');
    }
    Navigator walk; walk.setSort(SortSpec{SortKey::Size, false, 1});
    require(walk.open(w.root / "a" / "3.jpg", error), "open walk"); walk.setRoot(w.root);
    std::string forward = "a/3";
    for (int i = 0; i < 5; ++i) { const auto step = walk.step(1); require(step.has_value(), "walk ended early"); forward += " " + step->parent_path().filename().string() + "/" + step->stem().string(); }
    require(forward == "a/3 a/2 a/1 b/3 b/2 b/1" && !walk.step(1), ("walk skipped or reordered images: " + forward).c_str());
    std::string backward = "b/1";
    for (int i = 0; i < 5; ++i) { const auto step = walk.step(-1); require(step.has_value(), "backward walk ended early"); backward += " " + step->parent_path().filename().string() + "/" + step->stem().string(); }
    require(backward == "b/1 b/2 b/3 a/1 a/2 a/3", ("backward walk skipped images: " + backward).c_str());

    // The anchor exists right after open and after landing, so deleting the current image keeps next/previous defined.
    Navigator fresh; fresh.setSort(SortSpec{SortKey::Size, false, 1});
    require(fresh.open(w.root / "a" / "2.jpg", error), "open for anchor");
    fs::remove(w.root / "a" / "2.jpg");
    const auto afterOpen = fresh.step(1);
    require(afterOpen && afterOpen->filename() == "1.jpg", "deleting the just-opened image broke next");
    Navigator lander; lander.setSort(SortSpec{SortKey::Size, false, 1});
    require(lander.open(w.root / "b" / "3.jpg", error), "open for landing"); lander.setRoot(w.root);
    const auto landedOn = lander.folder(-1);   // explicit jump to the previous folder: natural last image of a
    require(landedOn && *landedOn == w.root / "a" / "3.jpg", "explicit jump left the natural endpoint");
    fs::remove(*landedOn);   // a/2 was removed above, so a/1 is the only image left in size order after a/3
    const auto afterLanding = lander.step(1);
    require(afterLanding && *afterLanding == w.root / "a" / "1.jpg", "deleting the landed image broke next");

    // Folder order and landing are independent of the file sort (NAV-02/NAV-06).
    Fixture t;
    for (const char* folder : {"a", "b"}) { t.file(fs::path(folder) / "1.jpg"); t.file(fs::path(folder) / "2.jpg"); t.file(fs::path(folder) / "3.jpg"); }
    Navigator tree; require(tree.open(t.root / "a" / "2.jpg", error), "open tree");
    tree.setRoot(t.root);
    tree.setSort(SortSpec{SortKey::Name, true, 1});
    const auto landed = tree.folder(1);
    require(landed && *landed == t.root / "b" / "1.jpg", "forward landing depended on file sort");
    require(names(tree.snapshot().files) == "3.jpg 2.jpg 1.jpg ", "list in new folder did not follow the sort");
    const auto back = tree.folder(-1);
    require(back && *back == t.root / "a" / "3.jpg", "backward landing depended on file sort");
}
void exactDirectoryNavigation() {
    Fixture f;
    const auto first=f.file("album/photo2.png");
    f.file("album/photo10.png"); f.file("album/readme.txt");
    f.file("album/child/hidden.png");
    fs::create_directories(f.root/"empty folder");
    Navigator nav; std::error_code error;
    require(nav.openDirectory(f.root/"album",error)&&!error,"exact album visit failed");
    require(nav.snapshot().directory==f.root/"album"&&nav.snapshot().current==first,"album visit did not select sorted first image");
    require(nav.snapshot().files.size()==2,"exact directory included descendants or unsupported files");
    require(nav.openDirectory(f.root/"empty folder",error),"empty directory rejected");
    require(nav.snapshot().directory==f.root/"empty folder"&&nav.snapshot().current.empty()&&nav.snapshot().files.empty(),"empty directory retained old image/list");
    const auto generation=nav.snapshot().generation;
    require(!nav.openDirectory(f.root/"missing",error)&&bool(error),"missing directory accepted");
    require(nav.snapshot().generation==generation&&nav.snapshot().directory==f.root/"empty folder","failed visit changed snapshot");
    require(!nav.openDirectory(first,error),"file accepted as directory");
    std::stop_source cancelled; cancelled.request_stop(); nav.setCancellation(cancelled.get_token());
    require(!nav.openDirectory(f.root/"album",error)&&error==std::errc::operation_canceled,"cancelled visit accepted");
    require(nav.snapshot().generation==generation,"cancelled visit changed snapshot");
}
int main() {
    try {
        exactDirectoryNavigation(); naturalOrdering(); traversal(); rootAndBoundaries(); filesystemMutation(); generatedOracle(); permissionBypass(); cancellation(); linksAndLaziness(); childFoldersAndSubtreeSearch(); fileSorting();
        std::cout << "navigation: all generated fixture tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "navigation failure: " << error.what() << '\n';
        return 1;
    }
}
