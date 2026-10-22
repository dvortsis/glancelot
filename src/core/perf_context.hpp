#pragma once
#include <cstdint>
#include "glancelot/perf_trace.hpp"
// Private PERF-MEASURE implementation detail: propagate a request generation through
// nested synchronous core calls on a worker, without changing frozen public APIs.
namespace glancelot::perf::detail {
inline std::int64_t begin() noexcept {
    return Recorder::instance().enabled() ? Recorder::nowNs() : -1;
}
inline void finish(Stage stage, std::uint64_t generation, std::int64_t start, std::uint8_t flags = None, std::uint64_t bytes = 0) noexcept {
    if (start >= 0) Recorder::instance().record({stage,flags,0,generation,start,Recorder::nowNs(),bytes});
}
inline std::uint64_t& currentGeneration() noexcept { thread_local std::uint64_t value{}; return value; }
struct GenerationContext {
    std::uint64_t previous;
    explicit GenerationContext(std::uint64_t value) noexcept : previous(currentGeneration()) { currentGeneration() = value; }
    ~GenerationContext() { currentGeneration() = previous; }
    GenerationContext(const GenerationContext&) = delete;
    GenerationContext& operator=(const GenerationContext&) = delete;
};
}
