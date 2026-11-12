# REL-01: build provenance rules (docs/RELEASING.md "Versions" and "Build provenance").
#
# Included by cmake/Provenance.cmake at configure time for its functions, and run as a script at build time:
#   cmake -DGLANCELOT_PROVENANCE_RUN=ON -DGLANCELOT_PROVENANCE_VERSION=<X.Y.Z-suffix> -DGLANCELOT_PROVENANCE_KIND=<kind>
#         -DGLANCELOT_PROVENANCE_SHA_OVERRIDE=<sha or empty> -DGLANCELOT_PROVENANCE_GIT=<git or empty>
#         -DGLANCELOT_PROVENANCE_SOURCE_DIR=<dir> -DGLANCELOT_PROVENANCE_TEMPLATE=<in> -DGLANCELOT_PROVENANCE_OUTPUT=<cpp>
#         -P cmake/ProvenanceGenerate.cmake
# The build-time run picks up a new commit without a reconfigure. It rewrites the generated file only when the version, SHA,
# kind, SOURCE_DATE_EPOCH or template change, so an ordinary rebuild of the same commit recompiles and relinks nothing.
# The build time is taken once, when the file is written, so every copy of it in one build is the same instant.

# Suffix and kind. An empty kind is inferred from the suffix: -rc.N is rc, -dev.N is dev, none is local. A release is never
# inferred; it is asked for with GLANCELOT_BUILD_KIND=release.
function(glancelot_provenance_resolve_kind out_var suffix kind)
  if(NOT suffix STREQUAL "" AND NOT suffix MATCHES "^-(rc|dev)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "GLANCELOT_VERSION_SUFFIX is \"${suffix}\"; it must be empty, -rc.N or -dev.N (N a number without leading zeros).")
  endif()
  set(suffix_kind "")
  if(suffix MATCHES "^-rc\\.")
    set(suffix_kind rc)
  elseif(suffix MATCHES "^-dev\\.")
    set(suffix_kind dev)
  endif()
  if(kind STREQUAL "")
    if(suffix_kind STREQUAL "")
      set(kind local)
    else()
      set(kind "${suffix_kind}")
    endif()
  endif()
  if(NOT kind MATCHES "^(release|rc|dev|local)$")
    message(FATAL_ERROR "GLANCELOT_BUILD_KIND is \"${kind}\"; it must be release, rc, dev or local (or empty, to infer it from the suffix).")
  endif()
  if(kind MATCHES "^(rc|dev)$" AND NOT suffix_kind STREQUAL kind)
    message(FATAL_ERROR "A ${kind} build needs GLANCELOT_VERSION_SUFFIX=-${kind}.N; it is \"${suffix}\".")
  endif()
  if(kind MATCHES "^(release|local)$" AND NOT suffix STREQUAL "")
    message(FATAL_ERROR "A ${kind} build has no version suffix; GLANCELOT_VERSION_SUFFIX is \"${suffix}\". Use -DGLANCELOT_BUILD_KIND=${suffix_kind} or clear the suffix.")
  endif()
  set(${out_var} "${kind}" PARENT_SCOPE)
endfunction()

# The full commit SHA (lowercase SHA-1 or SHA-256 hex), or "unknown". An explicit GLANCELOT_GIT_SHA wins (source archives and
# the Docker build, whose context has no .git). Otherwise Git is asked, and only about this source tree: a tree vendored inside
# another repository has no SHA of its own, rather than its host's. release and rc builds fail without one.
function(glancelot_provenance_resolve_sha out_var kind override git source_dir)
  set(sha "")
  set(reason "")
  if(NOT override STREQUAL "")
    glancelot_provenance_is_full_sha(override_ok "${override}")
    if(NOT override_ok)
      message(FATAL_ERROR "GLANCELOT_GIT_SHA is \"${override}\"; it must be a full 40- or 64-character hexadecimal commit SHA.")
    endif()
    string(TOLOWER "${override}" sha)
  elseif(git STREQUAL "" OR git MATCHES "-NOTFOUND$")
    set(reason "Git was not found")
  else()
    execute_process(COMMAND "${git}" -C "${source_dir}" rev-parse --show-toplevel
      RESULT_VARIABLE top_result OUTPUT_VARIABLE top ERROR_VARIABLE top_error
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT top_result STREQUAL "0")
      set(reason "${source_dir} is not a Git work tree (${top_result}: ${top_error})")
    else()
      file(REAL_PATH "${top}" top_real)
      file(REAL_PATH "${source_dir}" source_real)
      if(CMAKE_HOST_WIN32)
        string(TOLOWER "${top_real}" top_real)
        string(TOLOWER "${source_real}" source_real)
      endif()
      if(NOT top_real STREQUAL source_real)
        set(reason "${source_dir} is inside the Git work tree ${top}, not the root of its own")
      else()
        execute_process(COMMAND "${git}" -C "${source_dir}" rev-parse --verify "HEAD^{commit}"
          RESULT_VARIABLE head_result OUTPUT_VARIABLE head ERROR_VARIABLE head_error
          OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
        string(TOLOWER "${head}" head)
        glancelot_provenance_is_full_sha(head_ok "${head}")
        if(head_result STREQUAL "0" AND head_ok)
          set(sha "${head}")
        else()
          set(reason "git rev-parse HEAD failed (${head_result}: ${head_error})")
        endif()
      endif()
    endif()
  endif()
  if(sha STREQUAL "")
    if(kind MATCHES "^(release|rc)$")
      message(FATAL_ERROR "This ${kind} build must record its Git SHA, and none could be determined: ${reason}. "
        "Build from a Git checkout, or pass the commit with -DGLANCELOT_GIT_SHA=<full SHA>.")
    endif()
    set(sha unknown)
  endif()
  set(${out_var} "${sha}" PARENT_SCOPE)
endfunction()

# CMake regular expressions have no {n} quantifier: a full SHA is 40 (SHA-1) or 64 (SHA-256) hexadecimal digits.
function(glancelot_provenance_is_full_sha out_var value)
  string(LENGTH "${value}" length)
  if(value MATCHES "^[0-9a-fA-F]+$" AND (length EQUAL 40 OR length EQUAL 64))
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Writes the generated translation unit when its inputs changed. Sets <out_var> to TRUE when it wrote.
function(glancelot_provenance_write out_var template output version sha kind)
  file(SHA256 "${template}" template_hash)
  string(SUBSTRING "${template_hash}" 0 16 template_hash)
  set(epoch "$ENV{SOURCE_DATE_EPOCH}")
  if(NOT epoch STREQUAL "" AND NOT epoch MATCHES "^[0-9]+$")
    message(FATAL_ERROR "SOURCE_DATE_EPOCH is \"${epoch}\"; it must be a whole number of seconds since 1970-01-01 UTC.")
  endif()
  # No ";" in the key: file(STRINGS) would return it escaped, and the key would never match.
  set(key "version=${version}|sha=${sha}|kind=${kind}|epoch=${epoch}|template=${template_hash}")
  set(previous "")
  if(EXISTS "${output}")
    file(STRINGS "${output}" previous_lines REGEX "^// provenance-key: " LIMIT_COUNT 1)
    string(REPLACE "// provenance-key: " "" previous "${previous_lines}")
  endif()
  if(previous STREQUAL key)
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()
  # string(TIMESTAMP) honours SOURCE_DATE_EPOCH, for reproducible builds.
  string(TIMESTAMP GLANCELOT_PROVENANCE_BUILT_AT "%Y-%m-%dT%H:%M:%SZ" UTC)
  set(GLANCELOT_PROVENANCE_KEY "${key}")
  set(GLANCELOT_PROVENANCE_VERSION "${version}")
  set(GLANCELOT_PROVENANCE_SHA "${sha}")
  set(GLANCELOT_PROVENANCE_KIND "${kind}")
  configure_file("${template}" "${output}" @ONLY NEWLINE_STYLE LF)
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE AND GLANCELOT_PROVENANCE_RUN)
  if(NOT GLANCELOT_PROVENANCE_KIND MATCHES "^(release|rc|dev|local)$")
    message(FATAL_ERROR "GLANCELOT_PROVENANCE_KIND is \"${GLANCELOT_PROVENANCE_KIND}\"; configure resolves it before the build.")
  endif()
  glancelot_provenance_resolve_sha(run_sha "${GLANCELOT_PROVENANCE_KIND}" "${GLANCELOT_PROVENANCE_SHA_OVERRIDE}"
    "${GLANCELOT_PROVENANCE_GIT}" "${GLANCELOT_PROVENANCE_SOURCE_DIR}")
  glancelot_provenance_write(run_wrote "${GLANCELOT_PROVENANCE_TEMPLATE}" "${GLANCELOT_PROVENANCE_OUTPUT}"
    "${GLANCELOT_PROVENANCE_VERSION}" "${run_sha}" "${GLANCELOT_PROVENANCE_KIND}")
  if(run_wrote)
    message(STATUS "Glancelot provenance: ${GLANCELOT_PROVENANCE_VERSION} (${GLANCELOT_PROVENANCE_KIND}, ${run_sha})")
  endif()
endif()
