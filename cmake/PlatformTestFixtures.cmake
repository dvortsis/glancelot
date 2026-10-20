# Lead-frozen native launcher test isolation. Include only after the feature's
# real test sources exist. No production target or adapter is changed.
if(WIN32)
  message(FATAL_ERROR
    "Windows native launcher fixture is under a security hold after Defender Behavior:Win32/DefenseEvasion.A!ml. Do not restore/allow-list/rebuild the old helper. See docs/verification/PLATFORM-FIXTURE-SECURITY-01.md; a reviewed replacement is required.")
  qt_add_executable(platform_launcher_stub WIN32 tests/platform_launcher_stub.cpp)
  target_include_directories(platform_launcher_stub PRIVATE tests)
  target_link_libraries(platform_launcher_stub PRIVATE Qt6::Core shell32)
  target_sources(platform_integration_tests PRIVATE tests/platform_integration_test_adapter.cpp)
  target_include_directories(platform_integration_tests PRIVATE tests)
  target_compile_definitions(platform_integration_tests PRIVATE
    GLANCELOT_TEST_LAUNCH_STUB="$<TARGET_FILE:platform_launcher_stub>")
  target_link_libraries(platform_integration_tests PRIVATE Qt6::Core Qt6::Test shell32 ole32)
  add_dependencies(platform_integration_tests platform_launcher_stub)
else()
  target_link_libraries(platform_integration_tests PRIVATE glancelot_platform Qt6::Core Qt6::Test)
endif()
