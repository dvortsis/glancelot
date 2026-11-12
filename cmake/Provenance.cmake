# REL-01: build provenance (docs/RELEASING.md "Versions" and "Build provenance").
#
# Cache variables:
#   GLANCELOT_VERSION_SUFFIX  empty, -rc.N or -dev.N; appended to project(VERSION), never edited into it
#   GLANCELOT_BUILD_KIND      release, rc, dev or local; empty infers it from the suffix (a release is never inferred)
#   GLANCELOT_GIT_SHA         the full commit SHA, for a build without .git (source archive, Docker); empty asks Git
# release and rc builds fail here, at configure time, when the SHA can't be determined. Other builds record "unknown".
#
# After include():
#   GLANCELOT_VERSION_FULL     X.Y.Z plus the suffix
#   GLANCELOT_BUILD_KIND_RESOLVED, GLANCELOT_GIT_SHA_RESOLVED (configure-time values; the build refreshes the SHA)
#   glancelot_provenance_target(<target> OUTPUT <cpp> TEMPLATE <in> [SOURCE_DIR <dir>])
#   glancelot_add_build_info(<object library>)   the src/app/build_info unit (needs Qt6::Core and Qt6::Qml)
#   glancelot_provenance_windows_rc(<out var>)   a generated VERSIONINFO resource
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProvenanceGenerate.cmake")

set(GLANCELOT_VERSION_SUFFIX "" CACHE STRING "Pre-release suffix: empty, -rc.N or -dev.N (docs/RELEASING.md)")
set(GLANCELOT_BUILD_KIND "" CACHE STRING "Build kind: release, rc, dev or local; empty infers it from GLANCELOT_VERSION_SUFFIX")
set_property(CACHE GLANCELOT_BUILD_KIND PROPERTY STRINGS "" release rc dev local)
set(GLANCELOT_GIT_SHA "" CACHE STRING "Full Git commit SHA for a build without .git; empty asks Git")
if(NOT DEFINED GLANCELOT_PROVENANCE_SOURCE_DIR)
  set(GLANCELOT_PROVENANCE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
endif()
find_program(GIT_EXECUTABLE NAMES git git.cmd DOC "Git, for the build's commit SHA")

glancelot_provenance_resolve_kind(GLANCELOT_BUILD_KIND_RESOLVED "${GLANCELOT_VERSION_SUFFIX}" "${GLANCELOT_BUILD_KIND}")
set(GLANCELOT_VERSION_FULL "${PROJECT_VERSION}${GLANCELOT_VERSION_SUFFIX}")
glancelot_provenance_resolve_sha(GLANCELOT_GIT_SHA_RESOLVED "${GLANCELOT_BUILD_KIND_RESOLVED}" "${GLANCELOT_GIT_SHA}"
  "${GIT_EXECUTABLE}" "${GLANCELOT_PROVENANCE_SOURCE_DIR}")
if(GLANCELOT_GIT_SHA_RESOLVED STREQUAL "unknown" AND GLANCELOT_BUILD_KIND_RESOLVED STREQUAL "dev")
  message(WARNING "This dev build has no Git SHA and will report \"unknown\". Pass -DGLANCELOT_GIT_SHA=<full SHA> for a build that is handed out.")
endif()
message(STATUS "Glancelot provenance: ${GLANCELOT_VERSION_FULL} (${GLANCELOT_BUILD_KIND_RESOLVED}, ${GLANCELOT_GIT_SHA_RESOLVED})")

# Writes <OUTPUT> now, and adds <target>, which refreshes it on every build (a new commit needs no reconfigure). Ninja and
# Make see the file change only when its content does.
function(glancelot_provenance_target target)
  cmake_parse_arguments(arg "" "OUTPUT;TEMPLATE;SOURCE_DIR" "" ${ARGN})
  if(NOT arg_SOURCE_DIR)
    set(arg_SOURCE_DIR "${GLANCELOT_PROVENANCE_SOURCE_DIR}")
  endif()
  glancelot_provenance_write(wrote "${arg_TEMPLATE}" "${arg_OUTPUT}" "${GLANCELOT_VERSION_FULL}"
    "${GLANCELOT_GIT_SHA_RESOLVED}" "${GLANCELOT_BUILD_KIND_RESOLVED}")
  set(git "${GIT_EXECUTABLE}")
  if(git MATCHES "-NOTFOUND$")
    set(git "")
  endif()
  add_custom_target(${target}
    COMMAND "${CMAKE_COMMAND}" -DGLANCELOT_PROVENANCE_RUN=ON
      "-DGLANCELOT_PROVENANCE_VERSION=${GLANCELOT_VERSION_FULL}"
      "-DGLANCELOT_PROVENANCE_KIND=${GLANCELOT_BUILD_KIND_RESOLVED}"
      "-DGLANCELOT_PROVENANCE_SHA_OVERRIDE=${GLANCELOT_GIT_SHA}"
      "-DGLANCELOT_PROVENANCE_GIT=${git}"
      "-DGLANCELOT_PROVENANCE_SOURCE_DIR=${arg_SOURCE_DIR}"
      "-DGLANCELOT_PROVENANCE_TEMPLATE=${arg_TEMPLATE}"
      "-DGLANCELOT_PROVENANCE_OUTPUT=${arg_OUTPUT}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ProvenanceGenerate.cmake"
    BYPRODUCTS "${arg_OUTPUT}"
    COMMENT "Checking build provenance"
    VERBATIM)
endfunction()

# The build_info object library: stable sources plus the one generated translation unit. Linking it compiles nothing more
# when the commit changes; only build_info_values.cpp is rebuilt and the executables that link it are relinked.
function(glancelot_add_build_info target)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/provenance/build_info_values.cpp")
  glancelot_provenance_target(${target}_refresh OUTPUT "${generated}"
    TEMPLATE "${PROJECT_SOURCE_DIR}/src/app/build_info_values.cpp.in")
  add_library(${target} OBJECT
    "${PROJECT_SOURCE_DIR}/src/app/build_info.cpp" "${PROJECT_SOURCE_DIR}/src/app/build_info.hpp"
    "${PROJECT_SOURCE_DIR}/src/app/build_info_values.hpp" "${generated}")
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src/app")
  target_link_libraries(${target} PUBLIC Qt6::Core Qt6::Qml)
  add_dependencies(${target} ${target}_refresh)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# A VERSIONINFO resource for Windows executables, from the configure-time version (no SHA: it would go stale between
# reconfigures, and About and --version carry it).
function(glancelot_provenance_windows_rc out_var)
  set(GLANCELOT_RC_FILEFLAGS "0x0L")
  if(GLANCELOT_BUILD_KIND_RESOLVED MATCHES "^(rc|dev)$")
    set(GLANCELOT_RC_FILEFLAGS "VS_FF_PRERELEASE")
  elseif(GLANCELOT_BUILD_KIND_RESOLVED STREQUAL "local")
    set(GLANCELOT_RC_FILEFLAGS "VS_FF_PRIVATEBUILD")
  endif()
  set(rc "${CMAKE_CURRENT_BINARY_DIR}/provenance/glancelot_version.rc")
  configure_file("${PROJECT_SOURCE_DIR}/app/glancelot_version.rc.in" "${rc}" @ONLY NEWLINE_STYLE CRLF)
  set(${out_var} "${rc}" PARENT_SCOPE)
endfunction()
