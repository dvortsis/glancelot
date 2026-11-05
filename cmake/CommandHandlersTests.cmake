# EN-07: each view command a feature component handles, through the shipped Main.qml. Sources and environment mirror export_ui, because export-selection loads the dialog.
qt_add_executable(command_handlers_ui_tests tests/command_handlers_ui_tests.cpp
  src/app/selection_export_service.cpp src/app/selection_export_service.hpp
  ${GLANCELOT_VIEWER_SOURCES}
  ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
  src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
  src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
  src/render/image_canvas.cpp src/render/image_canvas.hpp)
target_include_directories(command_handlers_ui_tests PRIVATE include src/app src/render)
target_compile_definitions(command_handlers_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(command_handlers_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
qt_add_resources(command_handlers_ui_tests command_handlers_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
qt_add_resources(command_handlers_ui_tests command_handlers_brand_assets PREFIX "/" BASE assets FILES
  assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
  assets/brand/mascots/transparent/01_mascot_standing.png)
qt_add_shaders(command_handlers_ui_tests "command_handlers_canvas_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
glancelot_attach_monitor_customization(command_handlers_ui_tests)
add_dependencies(command_handlers_ui_tests glancelot-decoder)
add_test(NAME command_handlers_ui COMMAND command_handlers_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/command_handlers_ui-results.txt,txt" -o -,txt)
set_tests_properties(command_handlers_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 600)
glancelot_test_labels(command_handlers_ui LABELS ui process clipboard)
