# F3 (F072): GPS map handoff. Production registration is independent of BUILD_TESTING.
# gps_map_handoff.cpp is also in GLANCELOT_VIEWER_CONTEXT_SOURCES (cmake/ViewerSources.cmake) for the shipped-QML tests.
target_sources(glancelot PRIVATE src/app/gps_map_handoff.cpp src/app/gps_map_handoff.hpp)
qt_target_qml_sources(glancelot QML_FILES ui/GpsMapActions.qml ui/GpsMapSettings.qml)
if(BUILD_TESTING)
  # Shipped-QML suites with their own source lists: MetadataHud.qml needs the GpsMapHandoff type there too.
  foreach(f3_consumer IN ITEMS translation_coverage_ui_tests layout_presets_ui_tests)
    if(TARGET ${f3_consumer})
      target_sources(${f3_consumer} PRIVATE src/app/gps_map_handoff.cpp src/app/gps_map_handoff.hpp)
    endif()
  endforeach()
  # Validation, URL building and preferences on generated EXIF media, plus the shipped HUD, command and Settings page.
  qt_add_executable(gps_handoff_ui_tests tests/gps_handoff_ui_tests.cpp
    ${GLANCELOT_VIEWER_SOURCES}
    ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
    src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
    src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
    src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
    src/render/image_canvas.cpp src/render/image_canvas.hpp)
  target_include_directories(gps_handoff_ui_tests PRIVATE include src/app src/render)
  target_compile_definitions(gps_handoff_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(gps_handoff_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(gps_handoff_ui_tests gps_handoff_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(gps_handoff_ui_tests gps_handoff_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(gps_handoff_ui_tests "gps_handoff_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(gps_handoff_ui_tests)
  add_dependencies(gps_handoff_ui_tests glancelot-decoder)
  add_test(NAME gps_handoff_ui COMMAND gps_handoff_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/gps_handoff_ui-results.txt,txt" -o -,txt)
  set_tests_properties(gps_handoff_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
  glancelot_test_labels(gps_handoff_ui LABELS ui process clipboard)
endif()
