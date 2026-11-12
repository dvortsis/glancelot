# E6 (F052/F053 and the UX audit's theme contrast minors): derived theme roles, native high-contrast following and the
# --window-size clamp. The production code is header-only (src/app/theme_contrast.hpp, src/app/system_contrast.hpp,
# included by src/app/theme_manager.cpp), so no production target changes here.
if(BUILD_TESTING)
  qt_add_executable(theme_contrast_tests tests/theme_contrast_tests.cpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/settings_file.cpp include/glancelot/settings_file.hpp)
  target_include_directories(theme_contrast_tests PRIVATE include src/app)
  target_compile_definitions(theme_contrast_tests PRIVATE GLANCELOT_APP_PATH="$<TARGET_FILE:glancelot>")
  target_link_libraries(theme_contrast_tests PRIVATE Qt6::Gui Qt6::Test)
  qt_add_resources(theme_contrast_tests theme_contrast_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json)
  add_dependencies(theme_contrast_tests glancelot glancelot-decoder)
  add_test(NAME theme_contrast COMMAND theme_contrast_tests -o "${CMAKE_CURRENT_BINARY_DIR}/theme_contrast-results.txt,txt" -o -,txt)
  set_tests_properties(theme_contrast PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
  glancelot_test_labels(theme_contrast LABELS process)

  qt_add_executable(high_contrast_ui_tests tests/high_contrast_ui_tests.cpp
    ${GLANCELOT_VIEWER_SOURCES}
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp)
  target_include_directories(high_contrast_ui_tests PRIVATE include src/app)
  target_compile_definitions(high_contrast_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(high_contrast_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::Concurrent)
  qt_add_resources(high_contrast_ui_tests high_contrast_ui_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json)
  add_test(NAME high_contrast_ui COMMAND high_contrast_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/high_contrast_ui-results.txt,txt" -o -,txt)
  set_tests_properties(high_contrast_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
  glancelot_test_labels(high_contrast_ui LABELS ui)
endif()
