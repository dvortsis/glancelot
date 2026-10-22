#include "glancelot/selection_export.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
using namespace glancelot;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static void wr(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; }
static int countTmp(const fs::path& d) { int n = 0; for (auto& e : fs::directory_iterator(d)) if (e.path().filename().wstring().find(L".glancelot_export_") == 0) ++n; return n; }

int main() { std::cout << std::unitbuf;
    fs::path root = fs::temp_directory_path() / "glx_probe_r2";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root);
    auto src = root / "source.png"; wr(src, "source");
    auto snap = snapshotTarget(src, ec);
    ExportItem known{src, TargetKind::File, snap->identity, snap->bytes, snap->modified};

    // P1: Unicode destination WITH progress callback
    {
        ExportRequest q; q.items = {known};
        q.destination = root / fs::path(L"測試-\U0001f5bc.csv");
        try {
            auto r = exportSelection(q, {}, [](auto, auto) {});
            std::cout << "P1 unicode-dest+progress status=" << (int)r.status << " exists=" << fs::exists(q.destination) << "\n";
        } catch (const std::exception& e) {
            std::cout << "P1 unicode-dest+progress THREW: " << e.what() << " leftover-temp-files=" << countTmp(root) << "\n";
        }
        // without progress
        try {
            auto r = exportSelection(q);
            std::cout << "P1b unicode-dest no-progress status=" << (int)r.status << "\n";
        } catch (const std::exception& e) { std::cout << "P1b THREW " << e.what() << "\n"; }
    }
    // P2: cancellation at completion (done==total) with ordinary destination name
    for (const char* nm : {"plain.csv", "cancel-1.csv"}) {
        ExportRequest q; q.items = {known}; q.overwrite = true; q.destination = root / nm; wr(q.destination, "original");
        std::stop_source ss; int calls = 0; std::size_t lastDone = 99;
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto total) { ++calls; lastDone = done; if (done == total) ss.request_stop(); });
        std::cout << "P2 dest=" << nm << " status=" << (int)r.status << " (1=Cancelled) calls=" << calls << " lastDone=" << lastDone << " dest-preserved=" << (slurp(q.destination) == "original") << "\n";
    }
    // P2b: cancel when done==total-1 (last item start) with ordinary name, 2 items
    {
        auto s2 = root / "s2.png"; wr(s2, "s2"); auto sn2 = snapshotTarget(s2, ec);
        ExportItem k2{s2, TargetKind::File, sn2->identity, sn2->bytes, sn2->modified};
        ExportRequest q; q.items = {known, k2}; q.overwrite = true; q.destination = root / "two.csv"; wr(q.destination, "original");
        std::stop_source ss; int calls = 0;
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto total) { ++calls; if (done + 1 == total) ss.request_stop(); });
        std::cout << "P2b last-item-start cancel status=" << (int)r.status << " calls=" << calls << " dest-preserved=" << (slurp(q.destination) == "original") << " temp=" << countTmp(root) << "\n";
    }
    // P3: real junction alias to outside dir, lexical relative
    {
        fs::path rr = root / "rr"; fs::create_directories(rr);
        fs::path outside = root / "outside"; fs::create_directories(outside); wr(outside / "image.png", "img");
        std::string cmd = "cmd /c mklink /J \"" + (rr / "alias").string() + "\" \"" + outside.string() + "\" >nul";
        int rc = std::system(cmd.c_str());
        auto img = rr / "alias" / "image.png";
        ExportRequest q; q.destination = root / "junction.csv"; q.revalidate = false; q.pathPolicy = PathPolicy::RelativeToRoot; q.root = rr;
        q.columns = {ExportColumn::Path}; ExportItem it; it.path = img; q.items = {it};
        auto r = exportSelection(q);
        std::cout << "P3 junction rc=" << rc << " status=" << (int)r.status << " written=" << r.written << " skipped=" << r.skipped.size() << " csv=[" << slurp(q.destination) << "]\n";
    }
    // P4: root with trailing separator, and root == item, and sibling prefix
    {
        fs::path rr = root / "rr2"; fs::create_directories(rr); wr(rr / "a.png", "a");
        for (int variant = 0; variant < 3; ++variant) {
            ExportRequest q; q.destination = root / ("trail" + std::to_string(variant) + ".csv"); q.revalidate = false;
            q.pathPolicy = PathPolicy::RelativeToRoot; q.columns = {ExportColumn::Path, ExportColumn::Folder};
            q.root = variant == 0 ? fs::path(rr.native() + fs::path::preferred_separator) : (variant == 1 ? rr : fs::path(rr.native() + L"\\."));
            ExportItem it; it.path = rr / "a.png"; q.items = {it};
            auto r = exportSelection(q);
            std::cout << "P4 variant=" << variant << " root=[" << q.root.string() << "] written=" << r.written << " skipped=" << r.skipped.size() << " csv=[" << slurp(q.destination) << "]\n";
        }
    }
    // P5: no-overwrite race status
    {
        ExportRequest q; q.items = {known}; q.destination = root / "late.csv";
        bool c = false;
        auto r = exportSelection(q, {}, [&](auto, auto) { if (!c) { wr(q.destination, "late"); c = true; } });
        std::cout << "P5 race status=" << (int)r.status << " (5? DestinationExists=2) content=" << slurp(q.destination) << " temp=" << countTmp(root) << "\n";
    }
    // P6: perf, unknown identities, distinct paths, revalidate=false
    for (int n : {2000, 6000}) {
        ExportRequest q; q.destination = root / ("perf" + std::to_string(n) + ".csv"); q.revalidate = false;
        for (int i = 0; i < n; ++i) { ExportItem it; it.path = root / ("f" + std::to_string(i) + ".png"); q.items.push_back(it); }
        auto t0 = std::chrono::steady_clock::now();
        auto r = exportSelection(q);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "P6 unknown-identity n=" << n << " written=" << r.written << " ms=" << ms << "\n";
    }
    // P7: perf, known identities (revalidate=true real files)
    {
        int n = 3000; fs::path d = root / "many"; fs::create_directories(d);
        ExportRequest q; q.destination = root / "perfk.csv";
        for (int i = 0; i < n; ++i) { auto p = d / ("k" + std::to_string(i) + ".png"); wr(p, "x"); auto s = snapshotTarget(p, ec); q.items.push_back({p, TargetKind::File, s->identity, s->bytes, s->modified}); }
        auto t0 = std::chrono::steady_clock::now();
        auto r = exportSelection(q);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "P7 known-identity n=" << n << " written=" << r.written << " ms=" << ms << "\n";
    }
    fs::remove_all(root, ec);
    return 0;
}
