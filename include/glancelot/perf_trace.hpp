#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Lead-frozen PERF-MEASURE-01 contract (Codex lane). Local-only performance spans: no telemetry, no upload.
//  * Disabled by default; enabled by GLANCELOT_PERF_TRACE=1 or the app's --perf-report option. When disabled,
//    ScopedSpan costs one relaxed atomic load.
//  * Bounded: a fixed-capacity ring; overflow drops the oldest spans and counts them. No allocation per span.
//  * Monotonic clock (steady_clock) in nanoseconds; never wall time. Paths and EXIF are never recorded.
//  * Thread-safe record(); drain() is called by the reporter, never from the render thread.
namespace glancelot::perf {
enum class Stage : std::uint8_t {
    Enumerate, Sort, Publish, ThumbnailQueue, ThumbnailDecode, DecodeQueue, Decode, Upload, Present,
    CacheLookup, Mutation, Startup
};
enum Flag : std::uint8_t { None = 0, CacheHit = 1, Cancelled = 2, Failed = 4, Cold = 8, Stale = 16 };
struct Span {
    Stage stage{Stage::Startup};
    std::uint8_t flags{};
    std::uint32_t thread{};           // small stable per-thread index, not an OS id
    std::uint64_t generation{};       // the request's generation/revision, 0 when not applicable
    std::int64_t startNs{}, endNs{};  // steady_clock since process start
    std::uint64_t bytes{};            // payload size when meaningful (decoded bytes, listing entries)
};
struct ReportContext {
    std::string build;                // version + commit
    std::string platform, qtBackend;  // e.g. "windows-11-26200", "d3d11"
    std::string fixture;              // fixture manifest id; never a user path
};

class Recorder {
public:
    static Recorder& instance();
    bool enabled() const noexcept;
    void setEnabled(bool enabled) noexcept;
    void record(const Span& span) noexcept;
    std::vector<Span> drain();        // returns and clears recorded spans in start order
    std::uint64_t dropped() const noexcept;
    static std::size_t capacity() noexcept;   // fixed ring size (65536 spans)
    static std::int64_t nowNs() noexcept;
};

// RAII span; records on destruction when enabled. setFlags/setBytes may be called before it ends.
class ScopedSpan {
public:
    ScopedSpan(Stage stage, std::uint64_t generation = 0) noexcept;
    ~ScopedSpan();
    ScopedSpan(const ScopedSpan&) = delete; ScopedSpan& operator=(const ScopedSpan&) = delete;
    void setFlags(std::uint8_t flags) noexcept;
    void setBytes(std::uint64_t bytes) noexcept;
private:
    Span span_{}; bool active_{};
};

// Versioned JSON report ("schema": 1): context, per-stage count/min/p50/p95/max/total, flag splits
// (hit vs miss, cold vs warm, cancelled), dropped count and the raw spans when `includeSpans`.
std::string toJsonReport(const std::vector<Span>& spans, const ReportContext& context, std::uint64_t dropped,
                         bool includeSpans = false);
}
