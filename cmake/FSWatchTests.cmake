# FS-WATCH-01 backend, budget and real-QML routing test targets.
qt_add_executable(fs_watch_decoder_fixture tests/fs_watch_decoder_fixture.cpp)
target_link_libraries(fs_watch_decoder_fixture PRIVATE Qt6::Gui)
set(fs_watch_backend_sources
  src/app/thumbnail_service.cpp include/glancelot/thumbnail_service.hpp
  ${GLANCELOT_VIEWER_SOURCES}
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp)
qt_add_executable(fs_watch_tests tests/fs_watch_tests.cpp ${fs_watch_backend_sources})
qt_add_executable(fs_watch_budget_tests tests/fs_watch_budget_tests.cpp ${fs_watch_backend_sources})
foreach(fs_target IN ITEMS fs_watch_tests fs_watch_budget_tests)
  target_include_directories(${fs_target} PRIVATE include src/app)
  # Deterministic private seams exist only in these feature test executables.
  target_compile_definitions(${fs_target} PRIVATE GLANCELOT_FS_WATCH_TESTING)
  target_link_libraries(${fs_target} PRIVATE glancelot_core glancelot_platform
    Qt6::Gui Qt6::Test Qt6::Concurrent)
  add_dependencies(${fs_target} glancelot-decoder)
endforeach()
add_dependencies(fs_watch_tests fs_watch_decoder_fixture)
add_test(NAME fs_watch COMMAND fs_watch_tests -o "${CMAKE_CURRENT_BINARY_DIR}/fs-watch-results.txt,txt" -o -,txt)
add_test(NAME fs_watch_budget COMMAND fs_watch_budget_tests -o "${CMAKE_CURRENT_BINARY_DIR}/fs-watch-budget-results.txt,txt" -o -,txt)
set_tests_properties(fs_watch fs_watch_budget PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

# Lead-frozen real-QML routing regression. Required alongside backend coverage.
qt_add_executable(fs_watch_ui_tests tests/fs_watch_ui_tests.cpp
  ${GLANCELOT_VIEWER_SOURCES}
  ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
  src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
  src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
  src/render/image_canvas.cpp src/render/image_canvas.hpp)
target_include_directories(fs_watch_ui_tests PRIVATE include src/app src/render)
target_compile_definitions(fs_watch_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(fs_watch_ui_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
qt_add_resources(fs_watch_ui_tests fs_watch_ui_theme PREFIX "/" BASE assets
  FILES assets/themes/catalog.json ${ui_fonts})
qt_add_resources(fs_watch_ui_tests fs_watch_ui_brand PREFIX "/" BASE assets FILES
  assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
  assets/brand/mascots/transparent/01_mascot_standing.png)
qt_add_shaders(fs_watch_ui_tests "fs_watch_ui_shaders" PREFIX "/shaders"
  FILES src/render/image.vert src/render/image.frag)
add_dependencies(fs_watch_ui_tests glancelot-decoder)
add_test(NAME fs_watch_ui COMMAND fs_watch_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/fs-watch-ui-results.txt,txt" -o -,txt)
set_tests_properties(fs_watch_ui PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 90)
