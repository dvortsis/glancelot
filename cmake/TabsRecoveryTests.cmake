# STACK-F015-RECOVERY provisional target (S5): tab persistence through the production recovery session.
# Like recovery_controller_tests it compiles its own recovery engine with the private fault hooks, so it is marked as
# already attached. Viewer sources come from the EN-05 list (cmake/ViewerSources.cmake).
qt_add_executable(tabs_recovery_tests tests/tabs_recovery_tests.cpp
  ${GLANCELOT_VIEWER_SOURCES}
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  ${glancelot_recovery_provider_sources} ${glancelot_recovery_implementation_sources})
target_compile_definitions(tabs_recovery_tests PRIVATE GLANCELOT_RECOVERY_TESTING)
target_include_directories(tabs_recovery_tests PRIVATE include src/app)
target_link_libraries(tabs_recovery_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Gui Qt6::Test Qt6::Concurrent Threads::Threads)
set_property(TARGET tabs_recovery_tests PROPERTY GLANCELOT_RECOVERY_ATTACHED TRUE)
add_dependencies(tabs_recovery_tests glancelot-decoder)
qt_add_resources(tabs_recovery_tests tabs_recovery_catalog PREFIX "/"
  BASE assets FILES assets/themes/catalog.json)
add_test(NAME tabs_recovery COMMAND tabs_recovery_tests
  -o "${CMAKE_CURRENT_BINARY_DIR}/tabs_recovery-results.txt,txt" -o -,txt)
set_tests_properties(tabs_recovery PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
glancelot_test_labels(tabs_recovery LABELS process) # EN-03: starts glancelot-decoder
