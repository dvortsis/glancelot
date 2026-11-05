# B3 (F070): the export dialog and worker service, and their shipped-QML test target.
# Production registration is independent of BUILD_TESTING. The service registers its own QML type, and Main.qml loads
# the dialog by URL on first use, so executables that load Main.qml without this source are unaffected.
target_sources(glancelot PRIVATE src/app/selection_export_service.cpp src/app/selection_export_service.hpp)
qt_target_qml_sources(glancelot QML_FILES ui/ExportDialog.qml)
if(BUILD_TESTING)
  qt_add_executable(export_ui_tests tests/export_ui_tests.cpp
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
  target_include_directories(export_ui_tests PRIVATE include src/app src/render)
  target_compile_definitions(export_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(export_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(export_ui_tests export_ui_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(export_ui_tests export_ui_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(export_ui_tests "export_ui_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(export_ui_tests)
  add_dependencies(export_ui_tests glancelot-decoder)
  add_test(NAME export_ui COMMAND export_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/export_ui-results.txt,txt" -o -,txt)
  set_tests_properties(export_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
  glancelot_test_labels(export_ui LABELS ui process)
endif()
