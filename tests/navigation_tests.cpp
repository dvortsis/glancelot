#include "glancelot/navigation.hpp"
#include <algorithm>
#include <chrono>
#include <atomic>
#include <thread>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
int main() {
    try {
        naturalOrdering(); traversal(); rootAndBoundaries(); filesystemMutation(); generatedOracle(); permissionBypass(); cancellation(); linksAndLaziness();
        std::cout << "navigation: all generated fixture tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "navigation failure: " << error.what() << '\n';
        return 1;
    }
}
