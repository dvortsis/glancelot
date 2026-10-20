# Lead-frozen native launcher test isolation. Include only after the feature's
# real test sources exist. No production target or adapter is changed.
if(WIN32)
  foreach(fixture_source IN ITEMS tests/platform_fixture_capture.hpp tests/platform_argv_capture.cpp)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${fixture_source}")
      message(FATAL_ERROR "Retired Windows fixture is blocked. Reviewed replacement ${fixture_source} is required; see docs/PLATFORM-FIXTURE-SECURITY-01-PLAN.md. Do not restore or allow-list the old helper.")
    endif()
  endforeach()
  qt_add_executable(glancelot_test_argv_capture WIN32 tests/platform_argv_capture.cpp)
  target_link_libraries(glancelot_test_argv_capture PRIVATE Qt6::Core)
  set_target_properties(glancelot_test_argv_capture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native argv ☂ 🧪")
  target_sources(platform_integration_tests PRIVATE tests/platform_integration_test_adapter.cpp)
  target_include_directories(platform_integration_tests PRIVATE tests)
  target_compile_definitions(platform_integration_tests PRIVATE
    GLANCELOT_TEST_ARGV_CAPTURE="$<TARGET_FILE:glancelot_test_argv_capture>")
  target_link_libraries(platform_integration_tests PRIVATE Qt6::Core Qt6::Test shell32 ole32)
  add_dependencies(platform_integration_tests glancelot_test_argv_capture)
else()
  target_link_libraries(platform_integration_tests PRIVATE glancelot_platform Qt6::Core Qt6::Test)
endif()
