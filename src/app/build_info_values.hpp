#pragma once
// REL-01: the values of the one generated translation unit (build_info_values.cpp, written by cmake/ProvenanceGenerate.cmake
// from build_info_values.cpp.in). Read them through build_info.hpp; nothing else includes this header.
namespace glancelot::build::generated {
extern const char version[];  // project(VERSION) plus GLANCELOT_VERSION_SUFFIX
extern const char sha[];      // full lowercase commit SHA, or "unknown"
extern const char builtAt[];  // UTC, ISO 8601: YYYY-MM-DDTHH:MM:SSZ
extern const char kind[];     // release, rc, dev or local
}
