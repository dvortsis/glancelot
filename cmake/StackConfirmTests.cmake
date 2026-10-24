# STACK-CONFIRM-FIX: regressions from the whole-stack confirmation review (slideshow failures and navigation, details-mode anchor,
# type chips on the keyboard, long tab names). Sources follow ReferencePinTests.cmake (EN-05 viewer list, EN-04 context services).
if(BUILD_TESTING)
  qt_add_executable(stack_confirm_ui_tests tests/stack_confirm_ui_tests.cpp
    ${GLANCELOT_VIEWER_SOURCES}
    ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
    src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
    src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
    src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
    src/render/image_canvas.cpp src/render/image_canvas.hpp)
  target_include_directories(stack_confirm_ui_tests PRIVATE include src/render)
  target_compile_definitions(stack_confirm_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(stack_confirm_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(stack_confirm_ui_tests stack_confirm_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(stack_confirm_ui_tests stack_confirm_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(stack_confirm_ui_tests "stack_confirm_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(stack_confirm_ui_tests)
  add_dependencies(stack_confirm_ui_tests glancelot-decoder)
  add_test(NAME stack_confirm_ui COMMAND stack_confirm_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/stack_confirm_ui-results.txt,txt" -o -,txt)
  set_tests_properties(stack_confirm_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 400)
  glancelot_test_labels(stack_confirm_ui LABELS ui process)
endif()
