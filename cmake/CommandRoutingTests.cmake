# EN-06 provisional target (S3): command routing runs each command once through the shipped Main.qml.
# The lead may replace this file; sources and environment mirror input_ui.
qt_add_executable(command_routing_tests tests/command_routing_tests.cpp
  src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
  src/app/settings_file.cpp include/glancelot/settings_file.hpp
  src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
  src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
  src/app/folder_history.cpp include/glancelot/folder_history.hpp # provisional: not in glancelot_core
  src/render/image_canvas.cpp src/render/image_canvas.hpp)
target_include_directories(command_routing_tests PRIVATE include src/render)
target_compile_definitions(command_routing_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(command_routing_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
qt_add_resources(command_routing_tests routing_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
qt_add_resources(command_routing_tests routing_brand_assets PREFIX "/" BASE assets FILES
  assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
  assets/brand/mascots/transparent/01_mascot_standing.png)
qt_add_shaders(command_routing_tests "routing_canvas_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
glancelot_attach_monitor_customization(command_routing_tests)
add_dependencies(command_routing_tests glancelot-decoder)
add_test(NAME command_routing COMMAND command_routing_tests -o "${CMAKE_CURRENT_BINARY_DIR}/command_routing-results.txt,txt" -o -,txt)
set_tests_properties(command_routing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
