# Lead-frozen EXPORT-CSV-01 (F070) core source and test target. Included from the root CMakeLists.txt once
# src/core/selection_export.cpp exists on the feature branch; the integrator makes the include unconditional.
target_sources(glancelot_core PRIVATE src/core/selection_export.cpp)
if(BUILD_TESTING)
  add_executable(selection_export_tests tests/selection_export_tests.cpp)
  target_link_libraries(selection_export_tests PRIVATE glancelot_core Threads::Threads)
  add_test(NAME selection_export COMMAND selection_export_tests)
  set_tests_properties(selection_export PROPERTIES TIMEOUT 60)
endif()
