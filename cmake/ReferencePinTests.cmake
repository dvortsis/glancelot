# STACK-F040-PINNED provisional target (S5): parity of the F040 reference window and the MONITOR-OUTPUT pinned output.
# The lead may replace this with the EN-05 viewer source list.
if(BUILD_TESTING)
  qt_add_executable(reference_pin_tests tests/reference_pin_tests.cpp
    src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
    src/app/settings_file.cpp include/glancelot/settings_file.hpp
    src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
    src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
    src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
    src/render/image_canvas.cpp src/render/image_canvas.hpp)
  target_include_directories(reference_pin_tests PRIVATE include src/render)
  target_compile_definitions(reference_pin_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(reference_pin_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(reference_pin_tests reference_pin_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(reference_pin_tests reference_pin_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(reference_pin_tests "reference_pin_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(reference_pin_tests)
  add_dependencies(reference_pin_tests glancelot-decoder)
  add_test(NAME reference_pin COMMAND reference_pin_tests -o "${CMAKE_CURRENT_BINARY_DIR}/reference_pin-results.txt,txt" -o -,txt)
  set_tests_properties(reference_pin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
endif()
