# STARTUP-WINDOW-01: production implementation and dedicated tests are mandatory.
target_sources(glancelot PRIVATE src/app/startup_window_geometry.cpp src/app/startup_window_geometry.hpp)
if(BUILD_TESTING)
  qt_add_executable(startup_window_geometry_tests
    tests/startup_window_geometry_tests.cpp src/app/startup_window_geometry.cpp)
  target_include_directories(startup_window_geometry_tests PRIVATE src/app)
  target_link_libraries(startup_window_geometry_tests PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME startup_window_geometry COMMAND startup_window_geometry_tests -o -,txt)
  set_tests_properties(startup_window_geometry PROPERTIES TIMEOUT 60)
endif()
