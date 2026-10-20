# SAFE-MODE-01 lead-frozen feature wiring. The lead includes this file once real
# feature sources/tests exist, before candidate builds. No conditional test skip.
include("${CMAKE_CURRENT_LIST_DIR}/RecoverySources.cmake")

qt_add_executable(recovery_tests tests/recovery_tests.cpp
  ${glancelot_recovery_implementation_sources} ${glancelot_recovery_provider_sources}
  src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp)
target_compile_definitions(recovery_tests PRIVATE GLANCELOT_RECOVERY_TESTING)
target_include_directories(recovery_tests PRIVATE include src/app)
target_link_libraries(recovery_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Gui Qt6::Test Qt6::Concurrent)
add_dependencies(recovery_tests glancelot-decoder)
qt_add_resources(recovery_tests recovery_test_catalog PREFIX "/themes"
  BASE assets/themes FILES assets/themes/catalog.json)
add_test(NAME recovery COMMAND recovery_tests
  -o "${CMAKE_CURRENT_BINARY_DIR}/recovery-results.txt,txt" -o -,txt)
set_tests_properties(recovery PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

qt_add_executable(recovery_startup_tests tests/recovery_startup_tests.cpp
  app/startup_intent.cpp ${glancelot_recovery_provider_sources})
target_include_directories(recovery_startup_tests PRIVATE include src/app app)
target_link_libraries(recovery_startup_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Gui Qt6::Test Qt6::Concurrent)
qt_add_resources(recovery_startup_tests recovery_startup_catalog PREFIX "/themes"
  BASE assets/themes FILES assets/themes/catalog.json)
add_test(NAME recovery_startup COMMAND recovery_startup_tests
  -o "${CMAKE_CURRENT_BINARY_DIR}/recovery-startup-results.txt,txt" -o -,txt)
set_tests_properties(recovery_startup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

# Existing controller/slot consumers retain one definition of each source. This
# is enabled for the feature branch only; private fault hooks remain test-only.
get_property(recovery_existing_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(recovery_target IN LISTS recovery_existing_targets)
  get_target_property(recovery_existing_sources ${recovery_target} SOURCES)
  if(recovery_target STREQUAL "glancelot" OR
     "${recovery_existing_sources}" MATCHES "(viewer_controller|window_slot)[.]cpp")
    glancelot_attach_recovery(${recovery_target})
  endif()
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(recovery_process_args --executable "$<TARGET_FILE:glancelot>"
  --work-dir "${CMAKE_CURRENT_BINARY_DIR}/recovery-process")
if(UNIX AND NOT APPLE)
  find_program(RECOVERY_XVFB_EXECUTABLE Xvfb REQUIRED)
  list(APPEND recovery_process_args --xvfb "${RECOVERY_XVFB_EXECUTABLE}")
endif()
add_test(NAME recovery_process COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-recovery.py" ${recovery_process_args})
set_tests_properties(recovery_process PROPERTIES TIMEOUT 180)
