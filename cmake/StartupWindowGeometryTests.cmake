# Lead-frozen STARTUP-WINDOW-01 production/pure-value test wiring.
# Contract-only main has no implementation yet. Once the packet's .cpp exists,
# production and its dedicated test are mandatory; a missing test fails configure.
# The integrator removes this implementation-presence guard when landing the packet.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/startup_window_geometry.cpp")
  target_sources(glancelot PRIVATE src/app/startup_window_geometry.cpp src/app/startup_window_geometry.hpp)
  if(BUILD_TESTING)
    qt_add_executable(startup_window_geometry_tests
      tests/startup_window_geometry_tests.cpp src/app/startup_window_geometry.cpp)
    target_include_directories(startup_window_geometry_tests PRIVATE src/app)
    target_link_libraries(startup_window_geometry_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME startup_window_geometry COMMAND startup_window_geometry_tests -o -,txt)
    set_tests_properties(startup_window_geometry PROPERTIES TIMEOUT 60)
  endif()
endif()
