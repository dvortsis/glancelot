# B1 ASSET-GROUP. This file also adds the production source, so one include line in the root CMakeLists.txt is the whole
# root change: asset_group.cpp joins glancelot_platform as the contract says, and glancelot_platform now needs the core
# (snapshotTarget), which it did not before. Include it after every target below exists.
target_sources(glancelot_platform PRIVATE src/app/asset_group.cpp include/glancelot/asset_group.hpp src/app/asset_group_internal.hpp)
target_link_libraries(glancelot_platform PUBLIC glancelot_core)
if(BUILD_TESTING)
  # FileOperationService now expands through glancelot_platform.
  target_link_libraries(file_operation_service_tests PRIVATE glancelot_platform)
  qt_add_executable(asset_group_tests tests/asset_group_tests.cpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp src/app/platform_trash.hpp)
  target_include_directories(asset_group_tests PRIVATE include src/app)
  target_link_libraries(asset_group_tests PRIVATE glancelot_core glancelot_platform Qt6::Core Qt6::Test Qt6::Concurrent)
  add_test(NAME asset_group COMMAND asset_group_tests -o "${CMAKE_CURRENT_BINARY_DIR}/asset_group-results.txt,txt" -o -,txt)
  set_tests_properties(asset_group PROPERTIES TIMEOUT 120)
endif()
