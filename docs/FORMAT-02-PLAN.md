# Task Packet: FORMAT-02 (Common Rasters & RAW Previews)

**Milestone**: M3  
**Requirements**: `FMT-02`, `FMT-03`, `FMT-06`, `PERF-03`, `F035`  
**Owner Lane**: Gemini (`agent/*`)  
**Branch**: `agent/format`  
**Worktree**: `.worktrees/format`  
**Build Directory**: `build-format`  
**Memory Action**: `act_mvgbiqwz_f6226e0de873`  

---

## 1. Objective & Scope

Task `FORMAT-02` (Feature Opportunity `F035`) expands Glancelot's media pipeline to qualify common raster image formats and implement embedded-preview-first adapters for professional RAW camera files:

1. **Common Raster Coverage (`FMT-02`, `FMT-03`)**:
   - Qualify `.bmp`, `.tga`, `.tif`, `.tiff`, `.gif` (`FMT-02`).
   - Qualify `.webp`, `.apng` (`FMT-03`).
   - Track still and animated capabilities cleanly.
   - Update core navigation `isSupportedImage(const fs::path& path)` to recognize all qualified formats.

2. **RAW Embedded-Preview Adapters (`FMT-06`, `PERF-03`)**:
   - Qualify RAW camera extensions: `.dng`, `.cr2`, `.cr3`, `.nef`, `.nrw`, `.arw`, `.sr2`, `.raf`, `.orf`, `.rw2`, `.pef`, `.iiq`.
   - Implement fast, safe embedded JPEG preview extraction (`RawPreviewExtractor`):
     - Parses TIFF IFD structures (SubIFDs, `JPEGInterchangeFormat` / `0x0201`, `StripOffsets` / `0x0111`) and SOI marker fallbacks.
     - Extracts the camera's high-resolution embedded preview JPEG into memory without spawning external CLI dependencies or heavy slow decoders.
     - Decodes extracted preview directly to SDR working buffer (`QImage::Format_RGBA8888`).
   - Separate extraction, reading, and decoding timing metrics (`PERF-03`).
   - Explicit failure handling: absent or corrupt previews return clear, structured error messages without crashing.

3. **Subprocess Decoder Integration (`glancelot-decoder`)**:
   - Update `glancelot-decoder` main worker and `--thumbnail-server` to route RAW files through the embedded preview pipeline and common rasters through qualified image readers.
   - Preserve 256 MiB memory bounds and allocation limits.

---

## 2. Shared Interfaces & Contracts

### `include/glancelot/navigation.hpp`
- `bool isSupportedImage(const std::filesystem::path& path);`
  - Returns `true` for qualified rasters: `.jpg`, `.jpeg`, `.png`, `.bmp`, `.tga`, `.tif`, `.tiff`, `.gif`, `.webp`, `.apng`.
  - Returns `true` for qualified RAW formats: `.dng`, `.cr2`, `.cr3`, `.nef`, `.nrw`, `.arw`, `.sr2`, `.raf`, `.orf`, `.rw2`, `.pef`, `.iiq`.
  - Returns `false` for unsupported extensions (`.txt`, `.pdf`, `.exe`, `.docx`, etc.).
- `enum class ImageFormatCategory { Raster, Animated, Raw, Unsupported };`
- `ImageFormatCategory imageCategory(const std::filesystem::path& path);`

### `src/worker/raw_preview.hpp` (Decoder Helper)
- `struct RawPreviewResult { bool success{false}; QByteArray jpegData; QString error; int width{0}; int height{0}; };`
- `RawPreviewResult extractRawPreview(const QString& filePath);`
- `RawPreviewResult extractRawPreviewFromData(const uchar* data, qint64 size);`

---

## 3. Verification & Acceptance Criteria

- `tests/format_tests.cpp`:
  1. Common raster extension qualification: BMP, TGA, TIFF, GIF, WebP, APNG.
  2. RAW camera extension qualification: DNG, CR2, CR3, NEF, NRW, ARW, SR2, RAF, ORF, RW2, PEF, IIQ.
  3. Negative extension rejection (`.txt`, `.pdf`, `.mp4`, `.unknown`).
  4. Embedded preview extraction from TIFF-based RAW containers (DNG / NEF / CR2 / ARW).
  5. Fallback JPEG SOI search in RAW payloads.
  6. Explicit absent-preview behavior reporting.
  7. Malformed and truncated RAW input resilience.
  8. Thumbnail generation across new qualified rasters and RAW previews.
- Full Windows CTest suite (`scripts/build.ps1 -Test`) and Linux Docker build (`docker build -t glancelot-browser:main .`) pass with zero regressions.
