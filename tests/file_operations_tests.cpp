#include "glancelot/file_operations.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// DELETE-01 core tests. Generated, disposable fixtures under the system temp directory only; the "trash" here is a
// test backend that renames into a folder inside the fixture, so the real Trash/Recycle Bin is never touched.
// Cases the platform or account cannot create are reported as UNVERIFIED instead of failing.
namespace fs = std::filesystem;
using namespace glancelot;
namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}
void write(const fs::path& path, const std::string& text = "x") { std::ofstream(path, std::ios::binary) << text; }
struct Fixture {
    fs::path root;
    Fixture() {
        std::random_device random;
        root = fs::temp_directory_path() / ("glancelot-delete-" + std::to_string(random()));
        fs::create_directories(root / "bin");
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    fs::path dir(const std::string& name) { auto p = root / name; fs::create_directories(p); return p; }
};
// Directory link: a junction on Windows (no privilege needed), a symlink elsewhere.
bool directoryLink(const fs::path& link, const fs::path& target) {
#ifdef _WIN32
    const std::string command = "mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >nul 2>nul";
    return std::system(command.c_str()) == 0 && fs::exists(fs::symlink_status(link));
#else
    std::error_code ec; fs::create_directory_symlink(target, link, ec); return !ec;
#endif
}
// The test "trash": rename (never copy) into the fixture's bin; selected paths fail with a chosen outcome.
struct RenameTrash final : TrashBackend {
    fs::path bin;
    std::map<fs::path, DeleteOutcome> fail;
    std::function<void()> afterEach;
    int calls{};
    explicit RenameTrash(fs::path folder) : bin(std::move(folder)) {}
    DeleteResult moveToTrash(const MutationTarget& target) override {
        ++calls;
        DeleteResult out{target.path, DeleteOutcome::Trashed, {}};
        if (auto it = fail.find(target.path); it != fail.end()) { out.outcome = it->second; out.message = "simulated"; }
        else {
            std::error_code ec;
            fs::rename(target.path, bin / (std::to_string(calls) + "-" + target.path.filename().string()), ec);
            if (ec) { out.outcome = DeleteOutcome::Failed; out.message = ec.message(); }
        }
        if (afterEach) afterEach();
        return out;
    }
};
const RejectedTarget* rejected(const DeletionPlan& plan, const fs::path& path) {
    for (const auto& r : plan.rejected) if (r.path.lexically_normal() == path.lexically_normal()) return &r;
    return nullptr;
}
bool planned(const DeletionPlan& plan, const fs::path& path) {
    for (const auto& t : plan.targets) if (t.path == path.lexically_normal()) return true;
    return false;
}
fs::path rootPath() {
#ifdef _WIN32
    return fs::temp_directory_path().root_path();
#else
    return "/";
#endif
}
}

int main() {
    Fixture fx;
    {   // Deduplication: a descendant of another target and a repeated path are dropped as Duplicate.
        const auto album = fx.dir("dedupe/album");
        write(album / "a.jpg"); write(album / "b.jpg"); write(fx.root / "dedupe" / "loose.png");
        const auto plan = planDeletion({album / "a.jpg", album, fx.root / "dedupe" / "loose.png", fx.root / "dedupe" / "loose.png"});
        check(plan.targets.size() == 2 && planned(plan, album) && planned(plan, fx.root / "dedupe" / "loose.png"), "dedupe keeps ancestor + one copy");
        check(rejected(plan, album / "a.jpg") && rejected(plan, album / "a.jpg")->reason == PlanRejection::Duplicate, "descendant is Duplicate");
        check(plan.rejected.size() == 2, "repeated path is Duplicate once");
        check(plan.targets[0].path == album.lexically_normal(), "plan keeps request order");
        check(plan.targets[0].kind == TargetKind::Folder && plan.targets[0].contentFiles == 2 && plan.targets[0].contentCountComplete, "folder content count");
        // Exactly the named items: a same-stem sidecar next to a requested photo is not added.
        write(album / "a.xmp");
        const auto exact = planDeletion({album / "b.jpg"});
        check(exact.targets.size() == 1 && exact.rejected.empty(), "no implicit sidecars or pairs");
        std::cout << "dedupe ok\n";
    }
    {   // Roots, relative paths, device names and missing paths are rejected with a reason; the rest is still offered.
        write(fx.root / "keep.txt");
        std::vector<fs::path> request{rootPath(), fs::path("relative/file.txt"), fx.root / "missing.jpg", fx.root / "keep.txt"};
#ifdef _WIN32
        request.push_back(fs::path(L"\\\\server\\share"));
        request.push_back(fs::path(L"\\\\?\\C:\\Windows"));
#endif
        const auto plan = planDeletion(request);
        check(rejected(plan, rootPath()) && rejected(plan, rootPath())->reason == PlanRejection::Root, "filesystem root rejected");
        check(rejected(plan, "relative/file.txt") && rejected(plan, "relative/file.txt")->reason == PlanRejection::Inaccessible, "relative path rejected");
        check(rejected(plan, fx.root / "missing.jpg") && rejected(plan, fx.root / "missing.jpg")->reason == PlanRejection::Missing, "missing rejected");
        check(plan.targets.size() == 1 && planned(plan, fx.root / "keep.txt"), "valid target still offered");
#ifdef _WIN32
        const auto* unc = rejected(plan, L"\\\\server\\share");
        check(unc && unc->reason == PlanRejection::Root, "UNC share root rejected (lexical, no network access): "
              + (unc ? std::to_string(int(unc->reason)) + " " + unc->message : std::string("not found")));
        check(plan.rejected.size() == 5 && plan.rejected.back().reason == PlanRejection::Special, "device namespace rejected");
#else
        const auto fifo = fx.root / "pipe";
        if (::mkfifo(fifo.c_str(), 0600) == 0) {
            const auto special = planDeletion({fifo});
            check(special.targets.empty() && !special.rejected.empty() && special.rejected[0].reason == PlanRejection::Special, "FIFO rejected as Special");
        } else std::cout << "UNVERIFIED: FIFO fixture\n";
#endif
        std::cout << "rejections ok\n";
    }
    {   // Read-only volume rejection needs a read-only mount, which an unprivileged test cannot create. A harness can
        // supply an existing file on one (e.g. a Docker ":ro" bind mount of generated files).
        if (const char* file = std::getenv("GLANCELOT_READONLY_FILE")) {
            const auto plan = planDeletion({fs::path(file)});
            check(plan.targets.empty() && plan.rejected.size() == 1 && plan.rejected[0].reason == PlanRejection::ReadOnlyVolume,
                  "file on a read-only volume is rejected as ReadOnlyVolume");
            std::cout << "read-only volume rejection verified on " << file << "\n";
        } else std::cout << "UNVERIFIED: read-only volume rejection (set GLANCELOT_READONLY_FILE to a file on a read-only mount)\n";
    }
    {   // A link is a target in its own right: the link is trashed, its target survives; it is never counted through.
        const auto elsewhere = fx.dir("links/elsewhere");
        for (int i = 0; i < 5; ++i) write(elsewhere / ("hidden-" + std::to_string(i) + ".jpg"));
        const auto holder = fx.dir("links/holder");
        write(holder / "one.jpg");
        const auto inner = holder / "inner-link";
        const auto top = fx.root / "links" / "top-link";
        if (directoryLink(inner, elsewhere) && directoryLink(top, elsewhere)) {
            auto plan = planDeletion({holder});
            check(plan.targets.size() == 1 && plan.targets[0].contentFiles == 2 && plan.targets[0].contentFolders == 0,
                  "directory link inside a folder counts as one entry and is not followed (files=" + std::to_string(plan.targets.empty() ? 0 : plan.targets[0].contentFiles) + ")");
            plan = planDeletion({top});
            check(plan.targets.size() == 1 && plan.targets[0].link && plan.targets[0].contentFiles == 0, "link target is the link alone");
            RenameTrash trash(fx.root / "bin");
            const auto results = executeDeletion(plan, trash);
            check(results.size() == 1 && results[0].outcome == DeleteOutcome::Trashed, "link trashed");
            check(!fs::exists(fs::symlink_status(top)) && fs::exists(elsewhere / "hidden-0.jpg"), "link target survives");
            // A path reached through a link is never a target.
            const auto through = planDeletion({inner / "hidden-1.jpg"});
            check(through.targets.empty() && !through.rejected.empty() && through.rejected[0].reason == PlanRejection::Inaccessible, "path through a link rejected");
            check(fs::exists(elsewhere / "hidden-1.jpg"), "file behind a link untouched");
        } else std::cout << "UNVERIFIED: directory link fixtures could not be created\n";
        std::error_code ec;
        const auto fileTarget = fx.root / "links" / "real.txt"; write(fileTarget, "keep me");
        const auto fileLink = fx.root / "links" / "file-link.txt";
        fs::create_symlink(fileTarget, fileLink, ec);
        if (!ec) {
            const auto plan = planDeletion({fileLink});
            check(plan.targets.size() == 1 && plan.targets[0].link, "file symlink planned as a link");
            RenameTrash trash(fx.root / "bin");
            const auto results = executeDeletion(plan, trash);
            check(results[0].outcome == DeleteOutcome::Trashed && fs::exists(fileTarget), "file symlink trashed, target survives");
        } else std::cout << "UNVERIFIED: file symlink (needs Developer Mode or privilege on Windows): " << ec.message() << "\n";
        std::cout << "links ok\n";
    }
    {   // Bounded content walk: the shared budget stops the count, which is then incomplete ("at least").
        const auto big = fx.dir("bounded/big");
        for (int i = 0; i < 50; ++i) write(big / ("f" + std::to_string(i)));
        fs::create_directories(big / "sub" / "deeper");
        const auto plan = planDeletion({big}, {}, 10);
        check(plan.targets.size() == 1 && !plan.targets[0].contentCountComplete, "bounded count is incomplete");
        check(plan.targets[0].contentFiles + plan.targets[0].contentFolders == 10, "bounded count stops at the limit");
        const auto full = planDeletion({big});
        check(full.targets[0].contentCountComplete && full.targets[0].contentFiles == 50 && full.targets[0].contentFolders == 2, "unbounded count complete");
        std::cout << "bounded count ok\n";
    }
    {   // Revalidation: replaced, touched and removed files are reported and never handed to the backend.
        const auto d = fx.dir("revalidate");
        for (const char* name : {"replaced.jpg", "touched.jpg", "removed.jpg", "same.jpg"}) write(d / name, "original");
        const auto plan = planDeletion({d / "replaced.jpg", d / "touched.jpg", d / "removed.jpg", d / "same.jpg"});
        check(plan.targets.size() == 4, "revalidation plan");
        write(d / "incoming.tmp", "original"); // same size: only identity differs
        fs::rename(d / "incoming.tmp", d / "replaced.jpg");
        fs::last_write_time(d / "touched.jpg", fs::last_write_time(d / "touched.jpg") - std::chrono::hours(1));
        fs::remove(d / "removed.jpg");
        RenameTrash trash(fx.root / "bin");
        const auto results = executeDeletion(plan, trash);
        check(results.size() == 4, "one result per target");
        check(results[0].outcome == DeleteOutcome::Changed && fs::exists(d / "replaced.jpg"), "replaced file is Changed and kept");
        check(results[1].outcome == DeleteOutcome::Changed && fs::exists(d / "touched.jpg"), "touched file is Changed and kept");
        check(results[2].outcome == DeleteOutcome::Missing, "removed file is Missing");
        check(results[3].outcome == DeleteOutcome::Trashed && !fs::exists(d / "same.jpg"), "unchanged file trashed");
        check(trash.calls == 1, "backend only called for the valid target");
        // A grown file is Changed too.
        write(d / "grown.jpg", "a");
        const auto grownPlan = planDeletion({d / "grown.jpg"});
        { std::ofstream(d / "grown.jpg", std::ios::app) << "more"; }
        check(executeDeletion(grownPlan, trash)[0].outcome == DeleteOutcome::Changed, "size change is Changed");
        std::cout << "revalidation ok\n";
    }
    {   // Cancellation before and during a batch; an item already handed to the backend completes.
        const auto d = fx.dir("cancel");
        std::vector<fs::path> files;
        for (int i = 0; i < 4; ++i) { files.push_back(d / ("c" + std::to_string(i))); write(files.back()); }
        const auto plan = planDeletion(files);
        std::stop_source before; before.request_stop();
        RenameTrash trash(fx.root / "bin");
        auto results = executeDeletion(plan, trash, before.get_token());
        bool allCancelled = results.size() == 4;
        for (const auto& r : results) allCancelled = allCancelled && r.outcome == DeleteOutcome::Cancelled;
        check(allCancelled && trash.calls == 0 && fs::exists(files[0]), "cancel before: nothing attempted");
        std::stop_source during;
        trash.afterEach = [&] { during.request_stop(); };
        std::vector<std::pair<std::size_t, std::size_t>> steps;
        results = executeDeletion(plan, trash, during.get_token(), [&](std::size_t done, std::size_t total) { steps.emplace_back(done, total); });
        check(results[0].outcome == DeleteOutcome::Trashed && results[1].outcome == DeleteOutcome::Cancelled
              && results[3].outcome == DeleteOutcome::Cancelled, "cancel during: stops before the next item");
        check(fs::exists(files[1]) && fs::exists(files[3]) && trash.calls == 1, "cancelled items untouched");
        check(!steps.empty() && steps.front() == std::make_pair<std::size_t, std::size_t>(1, 4), "progress reported");
        // Planning honours cancellation too.
        std::stop_source stop; stop.request_stop();
        check(planDeletion(files, stop.get_token()).cancelled, "cancelled plan");
        std::cout << "cancellation ok\n";
    }
    {   // Backend failures leave files in place and are reported in plan order next to successes.
        const auto d = fx.dir("failures");
        std::vector<fs::path> files;
        for (const char* name : {"ok-1.jpg", "unavailable.jpg", "locked.jpg", "ok-2.jpg", "denied.jpg"}) { files.push_back(d / name); write(files.back()); }
        const auto plan = planDeletion(files);
        RenameTrash trash(fx.root / "bin");
        trash.fail[plan.targets[1].path] = DeleteOutcome::TrashUnavailable;
        trash.fail[plan.targets[2].path] = DeleteOutcome::InUse;
        trash.fail[plan.targets[4].path] = DeleteOutcome::Denied;
        const auto results = executeDeletion(plan, trash);
        const std::vector<DeleteOutcome> expected{DeleteOutcome::Trashed, DeleteOutcome::TrashUnavailable, DeleteOutcome::InUse, DeleteOutcome::Trashed, DeleteOutcome::Denied};
        bool order = results.size() == expected.size();
        for (std::size_t i = 0; order && i < results.size(); ++i) order = results[i].outcome == expected[i] && results[i].path == plan.targets[i].path;
        check(order, "partial success in plan order");
        check(!fs::exists(files[0]) && fs::exists(files[1]) && fs::exists(files[2]) && !fs::exists(files[3]) && fs::exists(files[4]), "failed items left in place");
        std::cout << "backend failures ok\n";
    }
    {   // snapshotTarget: identity is stable, missing is nullopt without error.
        write(fx.root / "snap.txt");
        std::error_code ec;
        const auto a = snapshotTarget(fx.root / "snap.txt", ec), b = snapshotTarget(fx.root / "snap.txt", ec);
        check(a && b && a->identity == b->identity && !(a->identity == FileIdentity{}), "stable identity");
        check(!snapshotTarget(fx.root / "nope.txt", ec) && !ec, "missing snapshot");
        std::cout << "snapshot ok\n";
    }
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "file_operations: all checks passed\n";
    return 0;
}
