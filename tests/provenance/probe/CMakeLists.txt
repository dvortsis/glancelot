# REL-01 scratch project for tests/provenance/run_configure_tests.cmake: the real cmake/Provenance.cmake in a project of
# its own (version 1.2.3, no compiler), so configure-time rules run without Qt. GLANCELOT_REPO names the checkout.
cmake_minimum_required(VERSION 3.24)
project(ProvenanceProbe VERSION 1.2.3 LANGUAGES NONE)
include("${GLANCELOT_REPO}/cmake/Provenance.cmake")
glancelot_provenance_target(probe_refresh OUTPUT "${CMAKE_BINARY_DIR}/build_info_values.cpp"
  TEMPLATE "${GLANCELOT_REPO}/src/app/build_info_values.cpp.in")
