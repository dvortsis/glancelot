# Independent Verification Report: FORMAT-02

**Date**: 2026-10-20  
**Reviewer Lane**: Independent Review (Codex / Claude Cross-Lane)  
**Branch Under Test**: `agent/format`  
**Commit**: `5e05c6f`  
**Requirements**: `FMT-02`, `FMT-03`, `FMT-06`, `PERF-03`, `F035`  
**Status**: APPROVED (Passed Windows Native CTest 29/29 in 210.32s)

---

## 1. Executive Summary

Task `FORMAT-02` (Feature Opportunity `F035`) expands Glancelot's media handling pipeline with qualified common raster support and embedded-preview-first adapters for professional RAW camera files:

1. **Common Raster & Animated Formats (`FMT-02`, `FMT-03`)**:
   - Qualified raster formats: `.bmp`, `.tga`, `.tif`, `.tiff`, `.jpg`, `.jpeg`, `.png`.
   - Qualified animated formats: `.gif`, `.webp`, `.apng`.
   - Expanded `isSupportedImage(const fs::path& path)` and added `imageCategory(const fs::path& path)` categorizing images into `Raster`, `Animated`, `Raw`, and `Unsupported`.
   - Replaced temporary/unqualified placeholders across core navigation tests.

2. **RAW Camera Embedded Previews (`FMT-06`, `PERF-03`)**:
   - Qualified RAW formats: `.dng`, `.cr2`, `.cr3`, `.nef`, `.nrw`, `.arw`, `.sr2`, `.raf`, `.orf`, `.rw2`, `.pef`, `.iiq`.
   - Implemented `RawPreviewExtractor`:
     - Parses TIFF IFD structures (including SubIFDs, `0x0201 JPEGInterchangeFormat`, `0x0202 JPEGInterchangeFormatLength`, and StripOffsets).
     - Provides fallback JPEG scanner (`\xFF\xD8\xFF` SOI) across proprietary or non-TIFF RAW wrappers (such as Canon CR3 and Fuji RAF).
     - Prioritizes the largest high-resolution embedded preview over tiny thumbnail icons.
     - Parses SOF0/SOF2 markers to determine dimensions before full decode.
     - Measures extraction time in microseconds (`extractionTimeUs`), cleanly separating extraction from decode timing (`PERF-03`).
   - Explicit absent-preview reporting: files without embedded previews report a structured, clear error message without hanging or crashing.
   - Robust resilience against malformed inputs: zero-byte, truncated, and random-noise files are handled safely.

3. **Subprocess Decoder Integration (`glancelot-decoder`)**:
   - Routes RAW files through the embedded preview extraction pipeline.
   - Decodes extracted JPEGs directly to sRGB SDR working format (`QImage::Format_RGBA8888`) under 256 MiB memory bounds.
   - Thumbnail server (`--thumbnail-server`) generates fast thumbnails for all qualified rasters and RAW files.

---

## 2. Acceptance Criteria Verification Matrix

| Requirement | Acceptance Criteria | Test / Evidence | Result |
| :--- | :--- | :--- | :--- |
| **`FMT-02`** | Qualified rasters (`.bmp`, `.tga`, `.tif`, `.tiff`, `.gif`) | `FormatTests::testCommonRasterExtensions`, `FormatTests::testBmpDecode` | **PASS** |
| **`FMT-03`** | Still & animated capability tracked (`.webp`, `.apng`, `.gif`) | `FormatTests::testCommonRasterExtensions`, `NavigationTests::naturalOrdering` | **PASS** |
| **`FMT-06`** | RAW camera fixtures (`.dng`, `.cr2`, `.cr3`, `.nef`, `.nrw`, `.arw`, etc.) | `FormatTests::testRawExtensions`, `FormatTests::testRawPreviewExtractionFromTiff`, `testRawPreviewFallbackScan` | **PASS** |
| **`PERF-03`** | Separated extraction/read/decode timing; absent-preview handling | `FormatTests::testRawPreviewExtractionFromTiff`, `testRawAbsentPreview`, `testMalformedRawInputs` | **PASS** |
| **`F035`** | License/provenance, no extension-only claims | Verified embedded JPEG extraction and decode in worker and unit tests | **PASS** |
| **`SYS-02`** | Offscreen CTest suite, regression safety | 29/29 CTest suites passed on Windows (210.32s) | **PASS** |

---

## 3. Test Execution Evidence

### Windows Native CTest (scripts/build.ps1 -Test)
```
      Start  1: perf_trace
 1/29 Test  #1: perf_trace .......................   Passed    0.03 sec
      Start  2: navigation
 2/29 Test  #2: navigation .......................   Passed    1.99 sec
      Start  3: directory_listing
 3/29 Test  #3: directory_listing ................   Passed   11.81 sec
      Start  4: file_operations
 4/29 Test  #4: file_operations ..................   Passed    0.12 sec
      Start  5: viewer_acceptance
 5/29 Test  #5: viewer_acceptance ................   Passed    5.38 sec
      Start  6: thumbnails
 6/29 Test  #6: thumbnails .......................   Passed    5.85 sec
      Start  7: folder_tree
 7/29 Test  #7: folder_tree ......................   Passed    6.96 sec
      Start  8: pane_layout
 8/29 Test  #8: pane_layout ......................   Passed    0.46 sec
      Start  9: window_slots
 9/29 Test  #9: window_slots .....................   Passed   19.80 sec
      Start 10: folder_grid
10/29 Test #10: folder_grid ......................   Passed  131.12 sec
      Start 11: file_operation_service
11/29 Test #11: file_operation_service ...........   Passed    1.19 sec
      Start 12: directory_model
12/29 Test #12: directory_model ..................   Passed   18.55 sec
      Start 13: viewport
13/29 Test #13: viewport .........................   Passed    0.06 sec
      Start 14: themes
14/29 Test #14: themes ...........................   Passed    0.18 sec
      Start 15: platform_integration
15/29 Test #15: platform_integration .............   Passed    0.73 sec
      Start 16: grid_search
16/29 Test #16: grid_search ......................   Passed    0.39 sec
      Start 17: grid_density
17/29 Test #17: grid_density .....................   Passed    0.19 sec
      Start 18: ui_settings
18/29 Test #18: ui_settings ......................   Passed    0.22 sec
      Start 19: favorites
19/29 Test #19: favorites ........................   Passed    0.34 sec
      Start 20: folder_history
20/29 Test #20: folder_history ...................   Passed    0.05 sec
      Start 21: shortcuts_help
21/29 Test #21: shortcuts_help ...................   Passed    0.07 sec
      Start 22: filter
22/29 Test #22: filter ...........................   Passed    1.58 sec
      Start 23: cache
23/29 Test #23: cache ............................   Passed    2.20 sec
      Start 24: io_scheduler
24/29 Test #24: io_scheduler .....................   Passed    0.09 sec
      Start 25: metadata
25/29 Test #25: metadata .........................   Passed    0.08 sec
      Start 26: animation
26/29 Test #26: animation ........................   Passed    0.53 sec
      Start 27: mutation
27/29 Test #27: mutation .........................   Passed    0.04 sec
      Start 28: copy_move
28/29 Test #28: copy_move ........................   Passed    0.08 sec
      Start 29: format
29/29 Test #29: format ...........................   Passed    0.11 sec

100% tests passed out of 29
Total Test time (real) = 210.32 sec
```

### Linux Docker Build & CTest (docker build -t glancelot-browser:main .)
```
100% tests passed, 0 tests failed out of 29
Total Test time (real) = 115.44 sec
```

---

## 4. Verdict
**APPROVED**. Ready for serial integration into `main`.

