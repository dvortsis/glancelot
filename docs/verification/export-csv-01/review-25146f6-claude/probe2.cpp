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
static int countTmp(const fs::path& d) { int n = 0; std::error_code ec; for (auto& e : fs::directory_iterator(d, ec)) if (e.path().filename().wstring().find(L".glancelot_export_") == 0) ++n; return n; }
static double msSince(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

int main() {
    std::cout << std::unitbuf;
    fs::path root = fs::temp_directory_path() / "glx_probe_r3";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root);
    auto src = root / "source.png"; wr(src, "source");
    auto snap = snapshotTarget(src, ec);
    ExportItem known{src, TargetKind::File, snap->identity, snap->bytes, snap->modified};

    // P1: Unicode destination file and directory WITH progress callback
    {
        fs::path d = root / fs::path(L"目錄-\U0001f4c1"); fs::create_directories(d);
        ExportRequest q; q.items = {known}; q.destination = d / fs::path(L"測試-\U0001f5bc.csv");
        try {
            auto r = exportSelection(q, {}, [](auto, auto) {});
            std::cout << "P1 unicode-dest+dir+progress status=" << (int)r.status << " exists=" << fs::exists(q.destination) << " temp=" << countTmp(d) << "\n";
        } catch (const std::exception& e) { std::cout << "P1 THREW: " << e.what() << " temp=" << countTmp(d) << "\n"; }
    }
    // P2: ordinary names, cancel at done==total (unreachable now), at done==0, at last-item start
    {
        ExportRequest q; q.items = {known}; q.overwrite = true; q.destination = root / "plain.csv"; wr(q.destination, "original");
        std::stop_source ss; int calls = 0; std::size_t lastDone = 99;
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto total) { ++calls; lastDone = done; if (done == total) ss.request_stop(); });
        std::cout << "P2a cancel@done==total plain.csv status=" << (int)r.status << " (0=Written: the callback never reports done==total) calls=" << calls << " lastDone=" << lastDone << "\n";
    }
    {
        ExportRequest q; q.items = {known}; q.overwrite = true; q.destination = root / "plain0.csv"; wr(q.destination, "original");
        std::stop_source ss; int calls = 0;
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto) { ++calls; if (done == 0) ss.request_stop(); });
        std::cout << "P2b cancel@done==0 status=" << (int)r.status << " calls=" << calls << " preserved=" << (slurp(q.destination) == "original") << " temp=" << countTmp(root) << "\n";
    }
    {
        auto s2 = root / "s2.png"; wr(s2, "s2"); auto sn2 = snapshotTarget(s2, ec);
        ExportItem k2{s2, TargetKind::File, sn2->identity, sn2->bytes, sn2->modified};
        ExportRequest q; q.items = {known, k2}; q.overwrite = true; q.destination = root / "two.csv"; wr(q.destination, "original");
        std::stop_source ss; int calls = 0;
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto total) { ++calls; if (done + 1 == total) ss.request_stop(); });
        std::cout << "P2c cancel@last-item-start status=" << (int)r.status << " calls=" << calls << " preserved=" << (slurp(q.destination) == "original") << " temp=" << countTmp(root) << "\n";
    }
    // P2d: progress is called from a name that contains "cancel-1" -> must behave like any other name
    {
        ExportRequest q; q.items = {known}; q.destination = root / "cancel-1.csv";
        std::size_t seen = 99; auto r = exportSelection(q, {}, [&](auto done, auto) { seen = done; });
        std::cout << "P2d dest=cancel-1.csv first progress done=" << seen << " (expect 0) status=" << (int)r.status << "\n";
    }
    // P6: timing, unknown identities, distinct paths, revalidate=false
    for (int n : {2000, 6000, 100000}) {
        ExportRequest q; q.destination = root / ("perf" + std::to_string(n) + ".csv"); q.revalidate = false; q.maxItems = 200000;
        for (int i = 0; i < n; ++i) { ExportItem it; it.path = root / ("f" + std::to_string(i) + ".png"); q.items.push_back(it); }
        auto t0 = std::chrono::steady_clock::now();
        auto r = exportSelection(q);
        std::cout << "P6 unknown-identity n=" << n << " status=" << (int)r.status << " written=" << r.written << " ms=" << msSince(t0) << "\n";
    }
    // P6b: cancellation latency with 100000 unknown items: stop at item 50000
    {
        ExportRequest q; q.destination = root / "perfc.csv"; q.revalidate = false; q.maxItems = 200000;
        for (int i = 0; i < 100000; ++i) { ExportItem it; it.path = root / ("c" + std::to_string(i) + ".png"); q.items.push_back(it); }
        std::stop_source ss; std::chrono::steady_clock::time_point tStop{};
        auto t0 = std::chrono::steady_clock::now();
        auto r = exportSelection(q, ss.get_token(), [&](auto done, auto) { if (done == 50000) { tStop = std::chrono::steady_clock::now(); ss.request_stop(); } });
        std::cout << "P6b cancel@50000/100000 status=" << (int)r.status << " stop-to-return-ms=" << msSince(tStop) << " total-ms=" << msSince(t0) << " temp=" << countTmp(root) << " dest-exists=" << fs::exists(q.destination) << "\n";
    }
    // P9: mixed identity both orders, unknown/unknown case variants, hardlink dedup, ".." normalisation
    {
        ExportItem unk = known; unk.identity = {0, 0};
        for (int order = 0; order < 2; ++order) {
            ExportRequest q; q.revalidate = false; q.destination = root / ("mix" + std::to_string(order) + ".csv");
            q.items = order ? std::vector<ExportItem>{unk, known} : std::vector<ExportItem>{known, unk};
            auto r = exportSelection(q);
            std::cout << "P9 mixed order=" << order << " written=" << r.written << " skipped=" << r.skipped.size() << " (expect 1/1)\n";
        }
        ExportItem upper = unk; upper.path = root / "SOURCE.PNG";
        ExportRequest q; q.revalidate = false; q.destination = root / "case.csv"; q.items = {unk, upper};
        auto r = exportSelection(q);
        std::cout << "P9 case-variant unknown/unknown written=" << r.written << " (Windows expect 1, Linux expect 2)\n";
        ExportItem dd = unk; dd.path = root / "sub" / ".." / "source.png";
        ExportRequest q2; q2.revalidate = false; q2.destination = root / "dots.csv"; q2.items = {unk, dd};
        auto r2 = exportSelection(q2);
        std::cout << "P9 dotdot-normalised unknown/unknown written=" << r2.written << " (expect 1)\n";
        auto hl = root / "hardlink.png"; fs::create_hard_link(src, hl, ec);
        auto sh = snapshotTarget(hl, ec);
        if (sh) {
            ExportItem h{hl, TargetKind::File, sh->identity, sh->bytes, sh->modified};
            ExportRequest q3; q3.destination = root / "hl.csv"; q3.items = {known, h};
            auto r3 = exportSelection(q3);
            std::cout << "P9 hardlink known/known written=" << r3.written << " skipped=" << r3.skipped.size() << " (expect 1/1)\n";
        }
        ExportItem diffId = known; diffId.path = root / "other.png"; wr(diffId.path, "o"); auto so = snapshotTarget(diffId.path, ec);
        diffId.identity = so->identity;
        ExportRequest q4; q4.destination = root / "distinct.csv"; q4.items = {known, diffId}; q4.revalidate = false;
        auto r4 = exportSelection(q4);
        std::cout << "P9 distinct known written=" << r4.written << " (expect 2)\n";
    }
    // P8: exception safety. Windows: path with lone surrogate; check outcome and leaked temp
#ifdef _WIN32
    {
        ExportItem bad; bad.path = root / fs::path(std::wstring(L"bad_\xD800.png"));
        ExportRequest q; q.revalidate = false; q.destination = root / "surrogate.csv"; q.items = {bad};
        try {
            auto r = exportSelection(q);
            std::cout << "P8 lone-surrogate status=" << (int)r.status << " written=" << r.written << " temp=" << countTmp(root) << "\n";
        } catch (const std::exception& e) { std::cout << "P8 lone-surrogate THREW: " << e.what() << " temp-after=" << countTmp(root) << "\n"; }
    }
#endif
    fs::remove_all(root, ec);
    return 0;
}
