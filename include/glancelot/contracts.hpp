#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace glancelot {
struct AssetId { std::filesystem::path path; };
struct SourceRevision { std::uintmax_t bytes{}; std::filesystem::file_time_type modified{}; std::uint64_t generation{}; };
enum class BoundaryMode { Stop, LoopFolder, ContinueTree };
struct NavigationSnapshot {
    std::filesystem::path root, directory, current;
    std::vector<std::filesystem::path> files;
    BoundaryMode boundary{BoundaryMode::ContinueTree};
    std::uint64_t generation{};
};
// DECODE-TIER (docs/contracts/DECODE-TIER.md). Preview < Fit < Full is one whole-image fidelity ladder. Tile is outside it (rank 0)
// and never replaces a base image. The names are the worker wire and QML spelling.
enum class DecodeTier : std::uint8_t { Preview, Fit, Full, Tile };
constexpr int tierRank(DecodeTier tier) {
    return tier == DecodeTier::Preview ? 1 : tier == DecodeTier::Fit ? 2 : tier == DecodeTier::Full ? 3 : 0;
}
constexpr std::string_view decodeTierName(DecodeTier tier) {
    return tier == DecodeTier::Preview ? "preview" : tier == DecodeTier::Fit ? "fit" : tier == DecodeTier::Full ? "full" : "tile";
}
constexpr std::optional<DecodeTier> parseDecodeTier(std::string_view name) {
    if (name == "preview") return DecodeTier::Preview;
    if (name == "fit") return DecodeTier::Fit;
    if (name == "full") return DecodeTier::Full;
    if (name == "tile") return DecodeTier::Tile;
    return std::nullopt;
}
inline constexpr int kFitBucketMinEdge = 512, kFitBucketMaxEdge = 8192; // powers of two; a bucket is a long edge in physical pixels
// Region in Full-resolution, display-oriented source pixels. Tile only; empty otherwise.
struct DecodeRegion {
    int x{}, y{}, width{}, height{};
    constexpr bool empty() const { return width <= 0 || height <= 0; }
};
struct DecodeRequest {
    AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U};
    DecodeTier tier{DecodeTier::Full};   // the default is the behaviour before tiers
    int sizeBucket{};                    // Fit: bucket long edge in physical pixels; 0 otherwise
    DecodeRegion region;                 // Tile only
    std::uint64_t generation{};          // the selection (or pane) generation that owns the request
};
enum class PixelFormat { Rgba8, Rgba16Float };
struct PixelBufferDescriptor {
    int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{};
    DecodeTier tier{DecodeTier::Full};
    int sourceWidth{}, sourceHeight{};   // display-oriented full source size; 0 = unknown (some Preview results)
    bool finalTier{true};                // no higher tier will follow (a RAW without a sensor decoder, a refused Full)
    std::string sourceToken;             // verified revision token; empty = unverified, so never cached
};
struct ViewportState { double physicalScale{1.0}, centerX{0.5}, centerY{0.5}; int quarterTurns{}; };
// INPUT-02: scope is "global" | "workspace" | "canvas" | "grid" | "tree" | "compare" (reserved for packet C3); a hold command activates on press and restores
// on release, focus loss, pointer cancel or image change. Defaults keep existing positional initializers valid.
struct Command { std::string id, label, defaultBinding; bool repeat{};
                 std::string scope{"global"};
                 bool hold{}; };
}
