#include "glancelot/perf_trace.hpp"
#include "../src/core/perf_context.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace glancelot::perf;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char**) {
    try {
        auto& recorder = Recorder::instance(); recorder.setEnabled(false); recorder.drain();
        const auto begin = Recorder::nowNs();
        constexpr int iterations = 1000000;
        for (int i = 0; i < iterations; ++i) { ScopedSpan disabled(Stage::Decode, std::uint64_t(i)); }
        const double disabledNs = double(Recorder::nowNs() - begin) / iterations;
        check(disabledNs < 1000, "disabled span exceeds 1000 ns mean budget");
        check(recorder.drain().empty(), "disabled recorder retained spans");
        recorder.setEnabled(true);
        { ScopedSpan scope(Stage::Decode, 42); scope.setBytes(4096); scope.setFlags(Cold | Failed); }
        auto single = recorder.drain();
        check(single.size() == 1 && single[0].generation == 42 && single[0].thread > 0
            && single[0].bytes == 4096 && single[0].flags == (Cold | Failed)
            && single[0].endNs >= single[0].startNs, "RAII attributes or monotonic clock incorrect");
        { detail::GenerationContext context(73); { ScopedSpan inherited(Stage::Sort); } }
        check(recorder.drain().at(0).generation == 73 && detail::currentGeneration() == 0, "generation context did not propagate/restore");
        constexpr std::size_t workers = 4, each = 20000;
        const auto oldDrops = recorder.dropped();
        std::array<std::thread, workers> threads;
        for (std::size_t t = 0; t < workers; ++t) threads[t] = std::thread([&, t] {
            for (std::size_t i = 0; i < each; ++i) recorder.record({Stage::Decode, None, std::uint32_t(t + 1), t,
                std::int64_t(each - i), std::int64_t(each - i + 10), 4});
        });
        for (auto& thread : threads) thread.join();
        auto drained = recorder.drain();
        check(drained.size() == Recorder::capacity(), "ring exceeds or fails to fill capacity");
        check(recorder.dropped() - oldDrops == workers * each - Recorder::capacity(), "overflow drop count incorrect");
        check(std::is_sorted(drained.begin(), drained.end(), [](const Span& a, const Span& b) { return a.startNs < b.startNs; }), "drain not in start order");
        check(recorder.drain().empty(), "drain did not clear ring");
        recorder.record({Stage::Decode, None, 1, 0, 20, 10, 0});
        check(recorder.drain().empty(), "negative duration admitted");
        std::vector<Span> known;
        for (int i = 1; i <= 100; ++i) known.push_back({Stage::CacheLookup, std::uint8_t(i % 2 ? Cold : CacheHit), 1, 1, 0, i, 0});
        known.push_back({Stage::Decode, Cancelled | Stale, 1, 2, 1, 10, 0});
        const auto json = toJsonReport(known, {"v1-test", "linux", "software", "fixture-v1"}, 7, true);
        check(json.find("\"p50_ns\":50") != std::string::npos && json.find("\"p95_ns\":95") != std::string::npos
            && json.find("\"total_ns\":5050") != std::string::npos, "known percentile arithmetic incorrect");
        const auto redacted = toJsonReport({}, {"C:\\private\\image.jpg", "/home/alice", "backend\nsecret", "https://secret"}, 0);
        check(redacted.find("private") == std::string::npos && redacted.find("alice") == std::string::npos
            && redacted.find("secret") == std::string::npos, "report leaks path-like context");
        const auto enabledBegin = Recorder::nowNs();
        for (int i = 0; i < 100000; ++i) { ScopedSpan span(Stage::Decode); }
        const double enabledNs = double(Recorder::nowNs() - enabledBegin) / 100000;
        recorder.drain(); recorder.setEnabled(false);
        if (argc > 1) std::cout << json << '\n';
        else std::cout << "PASS ring_bound multithread_drops drain_order flags percentiles redaction monotonic_clock\n"
            << "disabled_ns_per_span=" << disabledNs << " enabled_ns_per_span=" << enabledNs << " disabled_bound_ns=1000\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
