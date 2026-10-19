#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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
struct DecodeRequest { AssetId asset; SourceRevision revision; std::size_t memoryLimit{256U*1024U*1024U}; };
enum class PixelFormat { Rgba8, Rgba16Float };
struct PixelBufferDescriptor { int width{}, height{}, stride{}; PixelFormat format{PixelFormat::Rgba8}; std::string colorSpace{"sRGB"}; std::uint64_t generation{}; };
struct ViewportState { double physicalScale{1.0}, centerX{0.5}, centerY{0.5}; int quarterTurns{}; };
struct DisplayCapabilities { std::string backend; double devicePixelRatio{1.0}; bool hdr{}, monitorProfileManaged{}; };
// INPUT-02: scope is "global" | "workspace" | "canvas" | "grid" | "tree"; a hold command activates on press and restores
// on release, focus loss, pointer cancel or image change. Defaults keep existing positional initializers valid.
struct Command { std::string id, label, defaultBinding; bool repeat{};
                 std::string scope{"global"};
                 bool hold{}; };
struct DecoderCapability { std::string format; bool still{}, animated{}, multiPage{}, preview{}, highPrecision{}; };
}
