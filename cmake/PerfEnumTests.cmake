# A1 PERF-ENUM + F016: incremental folder listing, definitive order and 10k/100k traces.
# GLANCELOT_PERF_ENUM_TESTING enables the private enumeration seam in this executable only.
qt_add_executable(perf_enum_tests tests/perf_enum_tests.cpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp)
target_include_directories(perf_enum_tests PRIVATE include src/app tests)
target_compile_definitions(perf_enum_tests PRIVATE GLANCELOT_PERF_ENUM_TESTING)
target_link_libraries(perf_enum_tests PRIVATE glancelot_core Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME perf_enum COMMAND perf_enum_tests -o "${CMAKE_CURRENT_BINARY_DIR}/perf-enum-results.txt,txt" -o -,txt)
set_tests_properties(perf_enum PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
glancelot_test_labels(perf_enum LABELS budget)

# The shipped ui/Main.qml over the same seam: viewport anchors and cursor placement across the definitive order.
qt_add_executable(perf_enum_ui_tests tests/perf_enum_ui_tests.cpp
  ${GLANCELOT_VIEWER_SOURCES}
  ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
  src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
  src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
  src/render/image_canvas.cpp src/render/image_canvas.hpp)
target_include_directories(perf_enum_ui_tests PRIVATE include src/app src/render tests)
target_compile_definitions(perf_enum_ui_tests PRIVATE GLANCELOT_PERF_ENUM_TESTING GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(perf_enum_ui_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
qt_add_resources(perf_enum_ui_tests perf_enum_ui_theme PREFIX "/" BASE assets
  FILES assets/themes/catalog.json ${ui_fonts})
qt_add_resources(perf_enum_ui_tests perf_enum_ui_brand PREFIX "/" BASE assets FILES
  assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
  assets/brand/mascots/transparent/01_mascot_standing.png)
qt_add_shaders(perf_enum_ui_tests "perf_enum_ui_shaders" PREFIX "/shaders"
  FILES src/render/image.vert src/render/image.frag)
glancelot_attach_monitor_customization(perf_enum_ui_tests)
add_dependencies(perf_enum_ui_tests glancelot-decoder)
add_test(NAME perf_enum_ui COMMAND perf_enum_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/perf-enum-ui-results.txt,txt" -o -,txt)
set_tests_properties(perf_enum_ui PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
glancelot_test_labels(perf_enum_ui LABELS ui process)
