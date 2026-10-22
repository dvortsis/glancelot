#include "glancelot/perf_trace.hpp"
#include "perf_context.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>

namespace glancelot::perf {
namespace {
constexpr std::size_t ringCapacity = 65536;
constexpr std::array<const char*, 12> names{"enumerate", "sort", "publish", "thumbnail_queue",
    "thumbnail_decode", "decode_queue", "decode", "upload", "present", "cache_lookup", "mutation", "startup"};
const auto origin = std::chrono::steady_clock::now();
struct State {
    std::atomic<bool> enabled{[] { const char* v = std::getenv("GLANCELOT_PERF_TRACE"); return v && std::string_view(v) == "1"; }()};
    std::atomic<std::uint32_t> nextThread{1};
    std::atomic<std::uint64_t> dropped{0};
    std::mutex mutex;
    std::array<Span, ringCapacity> ring{};
    std::size_t head{}, count{};
};
State state;
Recorder recorder;
std::uint32_t threadIndex() noexcept {
    thread_local const auto index = state.nextThread.fetch_add(1, std::memory_order_relaxed);
    return index;
}
bool valid(const Span& span) {
    return std::size_t(span.stage) < names.size() && span.startNs >= 0 && span.endNs >= span.startNs;
}
// Context is an identifier, never a free-form diagnostic. Reject path-like values as a whole.
std::string identifier(const std::string& value) {
    if (value.size() > 256) return "redacted";
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '.' || c == '-' || c == '_' || c == '+' || c == ' ')) return "redacted";
    return value;
}
void addSaturated(std::uint64_t& total, std::uint64_t value) {
    total = value > std::numeric_limits<std::uint64_t>::max() - total
        ? std::numeric_limits<std::uint64_t>::max() : total + value;
}
std::string statistics(const std::vector<const Span*>& spans) {
    std::vector<std::int64_t> values; values.reserve(spans.size());
    std::uint64_t total{}, bytes{};
    for (const auto* span : spans) {
        const auto duration = span->endNs - span->startNs;
        values.push_back(duration); addSaturated(total, std::uint64_t(duration)); addSaturated(bytes, span->bytes);
    }
    std::sort(values.begin(), values.end());
    const auto percentile = [&](std::size_t percent) {
        return values.empty() ? std::int64_t{} : values[(values.size() * percent + 99) / 100 - 1];
    };
    std::ostringstream out;
    out << "{\"count\":" << values.size() << ",\"min_ns\":" << (values.empty() ? 0 : values.front())
        << ",\"p50_ns\":" << percentile(50) << ",\"p95_ns\":" << percentile(95)
        << ",\"max_ns\":" << (values.empty() ? 0 : values.back()) << ",\"total_ns\":" << total
        << ",\"bytes\":" << bytes << '}';
    return out.str();
}
}
Recorder& Recorder::instance() { return recorder; }
bool Recorder::enabled() const noexcept { return state.enabled.load(std::memory_order_relaxed); }
void Recorder::setEnabled(bool value) noexcept { state.enabled.store(value, std::memory_order_relaxed); }
void Recorder::record(const Span& span) noexcept {
    if (!enabled() || !valid(span)) return;
    std::lock_guard lock(state.mutex);
    if (state.count == ringCapacity) {
        state.head = (state.head + 1) % ringCapacity;
        state.dropped.fetch_add(1, std::memory_order_relaxed);
        --state.count;
    }
    auto& stored = state.ring[(state.head + state.count++) % ringCapacity];
    stored = span;
    if (!stored.thread) stored.thread = threadIndex();
}
std::vector<Span> Recorder::drain() {
    std::vector<Span> result;
    {
        std::lock_guard lock(state.mutex);
        result.reserve(state.count);
        for (std::size_t i = 0; i < state.count; ++i) result.push_back(state.ring[(state.head + i) % ringCapacity]);
        state.head = state.count = 0;
    }
    std::stable_sort(result.begin(), result.end(), [](const Span& a, const Span& b) { return a.startNs < b.startNs; });
    return result;
}
std::uint64_t Recorder::dropped() const noexcept { return state.dropped.load(std::memory_order_relaxed); }
std::size_t Recorder::capacity() noexcept { return ringCapacity; }
std::int64_t Recorder::nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - origin).count();
}
ScopedSpan::ScopedSpan(Stage stage, std::uint64_t generation) noexcept {
    active_ = Recorder::instance().enabled();
    if (active_) { span_.stage = stage; span_.generation = generation ? generation : detail::currentGeneration(); span_.thread = threadIndex(); span_.startNs = Recorder::nowNs(); }
}
ScopedSpan::~ScopedSpan() {
    if (active_) { span_.endNs = Recorder::nowNs(); Recorder::instance().record(span_); }
}
void ScopedSpan::setFlags(std::uint8_t flags) noexcept { span_.flags = flags; }
void ScopedSpan::setBytes(std::uint64_t bytes) noexcept { span_.bytes = bytes; }

std::string toJsonReport(const std::vector<Span>& spans, const ReportContext& context, std::uint64_t dropped, bool includeSpans) {
    std::ostringstream out;
    out << "{\"schema\":1,\"clock\":\"steady_process_relative_ns\",\"percentile\":\"nearest_rank\",\"context\":{\"build\":\""
        << identifier(context.build) << "\",\"platform\":\"" << identifier(context.platform) << "\",\"qt_backend\":\""
        << identifier(context.qtBackend) << "\",\"fixture\":\"" << identifier(context.fixture) << "\"},\"dropped\":" << dropped << ",\"stages\":{";
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::vector<const Span*> stage;
        for (const auto& span : spans) if (valid(span) && std::size_t(span.stage) == i) stage.push_back(&span);
        if (i) out << ',';
        out << '"' << names[i] << "\":{\"all\":" << statistics(stage) << ",\"splits\":{";
        constexpr std::array<const char*, 8> splits{"hit", "miss", "cold", "warm", "cancelled", "failed", "stale", "useful"};
        for (std::size_t j = 0; j < splits.size(); ++j) {
            std::vector<const Span*> selected;
            for (const auto* span : stage) {
                const auto flags = span->flags;
                const bool match = j == 0 ? bool(flags & CacheHit) : j == 1 ? !bool(flags & CacheHit)
                    : j == 2 ? bool(flags & Cold) : j == 3 ? !bool(flags & Cold)
                    : j == 4 ? bool(flags & Cancelled) : j == 5 ? bool(flags & Failed)
                    : j == 6 ? bool(flags & Stale) : !(flags & (Cancelled | Failed | Stale));
                if (match) selected.push_back(span);
            }
            if (j) out << ',';
            out << '"' << splits[j] << "\":" << statistics(selected);
        }
        out << "}}";
    }
    out << '}';
    if (includeSpans) {
        out << ",\"spans\":["; bool first = true;
        for (const auto& span : spans) {
            if (!valid(span)) continue;
            if (!first) out << ',';
            first = false;
            out << "{\"stage\":\"" << names[std::size_t(span.stage)] << "\",\"flags\":" << unsigned(span.flags)
                << ",\"thread\":" << span.thread << ",\"generation\":" << span.generation << ",\"start_ns\":"
                << span.startNs << ",\"end_ns\":" << span.endNs << ",\"bytes\":" << span.bytes << '}';
        }
        out << ']';
    }
    out << '}'; return out.str();
}
}
