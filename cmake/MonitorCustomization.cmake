# MONITOR-CUSTOM-01 lead-owned registration. The root source-presence guard is
# temporary while Claude implements the packet; remove it before integration.
include_guard(GLOBAL)
foreach(monitor_required IN ITEMS
    src/app/monitor_customization.cpp
    ui/MonitorSettings.qml ui/DetachedPaneHost.qml
    tests/monitor_customization_tests.cpp tests/monitor_customization_ui_tests.cpp)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${monitor_required}")
    message(FATAL_ERROR "MONITOR-CUSTOM-01 requires ${monitor_required}")
  endif()
endforeach()

function(glancelot_attach_monitor_customization monitor_target)
  get_target_property(monitor_existing ${monitor_target} SOURCES)
  foreach(monitor_source IN ITEMS
      src/app/monitor_customization.cpp include/glancelot/monitor_customization.hpp
      src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
      src/app/settings_file.cpp include/glancelot/settings_file.hpp
      src/app/startup_window_geometry.cpp src/app/startup_window_geometry.hpp
      src/render/presentation_window.cpp src/render/presentation_window.hpp
      src/render/image_canvas.cpp src/render/image_canvas.hpp)
    list(FIND monitor_existing "${monitor_source}" monitor_index)
    if(monitor_index EQUAL -1)
      target_sources(${monitor_target} PRIVATE "${monitor_source}")
    endif()
  endforeach()
  target_include_directories(${monitor_target} PRIVATE include src/app src/render)
  target_link_libraries(${monitor_target} PRIVATE Qt6::Quick Qt6::GuiPrivate Qt6::Concurrent)
endfunction()

# Production registration must be independent of BUILD_TESTING.
glancelot_attach_monitor_customization(glancelot)
qt_target_qml_sources(glancelot QML_FILES ui/MonitorSettings.qml ui/DetachedPaneHost.qml)

if(BUILD_TESTING)
  # STACK-F054-COVERAGE provisional: translation_coverage_ui shares this harness; the lead may move it to its own fragment.
  foreach(monitor_suite IN ITEMS monitor_customization monitor_customization_ui translation_coverage_ui)
    qt_add_executable(${monitor_suite}_tests tests/${monitor_suite}_tests.cpp
      src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
      src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
      src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
      src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
      src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
      src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp)
    target_link_libraries(${monitor_suite}_tests PRIVATE glancelot_core glancelot_platform
      Qt6::QuickControls2 Qt6::Test)
    glancelot_attach_monitor_customization(${monitor_suite}_tests)
    target_compile_definitions(${monitor_suite}_tests PRIVATE
      GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qt_add_resources(${monitor_suite}_tests ${monitor_suite}_theme PREFIX "/" BASE assets
      FILES assets/themes/catalog.json ${ui_fonts})
    qt_add_resources(${monitor_suite}_tests ${monitor_suite}_brand PREFIX "/" BASE assets FILES
      assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
      assets/brand/mascots/transparent/01_mascot_standing.png)
    qt_add_shaders(${monitor_suite}_tests "${monitor_suite}_shaders" PREFIX "/shaders"
      FILES src/render/image.vert src/render/image.frag)
    add_dependencies(${monitor_suite}_tests glancelot-decoder)
    add_test(NAME ${monitor_suite} COMMAND ${monitor_suite}_tests -o "${CMAKE_CURRENT_BINARY_DIR}/${monitor_suite}-results.txt,txt" -o -,txt)
    set_tests_properties(${monitor_suite} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
  endforeach()
  # Existing shipped-QML fixtures must supply the real new context provider.
  foreach(monitor_consumer IN ITEMS input_ui_tests pathbar_ui_tests grid_ui_tests fs_watch_ui_tests monitor_output_tests)
    if(TARGET ${monitor_consumer})
      glancelot_attach_monitor_customization(${monitor_consumer})
    endif()
  endforeach()
endif()
