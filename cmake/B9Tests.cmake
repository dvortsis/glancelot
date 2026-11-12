# B9 (F057): the in-app restore of a recovered workspace. The offer class is in the EN-05 viewer list
# (cmake/ViewerSources.cmake); this file adds its QML component to the app module and the two test targets.
# Production registration is independent of BUILD_TESTING.
qt_target_qml_sources(glancelot QML_FILES ui/WorkspaceRestore.qml)
if(BUILD_TESTING)
  # Controller level: the offer against the production recovery engine (attached by glancelot_attach_recovery_consumers).
  qt_add_executable(restore_offer_tests tests/restore_offer_tests.cpp ${GLANCELOT_VIEWER_SOURCES})
  target_include_directories(restore_offer_tests PRIVATE include src/app)
  target_link_libraries(restore_offer_tests PRIVATE glancelot_core glancelot_platform Qt6::Gui Qt6::Test Qt6::Concurrent Threads::Threads)
  add_dependencies(restore_offer_tests glancelot-decoder)
  add_test(NAME restore_offer COMMAND restore_offer_tests -o "${CMAKE_CURRENT_BINARY_DIR}/restore_offer-results.txt,txt" -o -,txt)
  set_tests_properties(restore_offer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
  glancelot_test_labels(restore_offer LABELS process) # EN-03: starts glancelot-decoder

  # The shipped Main.qml: the banner, its keyboard path and its accessible text.
  qt_add_executable(restore_ui_tests tests/restore_ui_tests.cpp
    ${GLANCELOT_VIEWER_SOURCES}
    ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
    src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
    src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
    src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
    src/render/image_canvas.cpp src/render/image_canvas.hpp)
  target_include_directories(restore_ui_tests PRIVATE include src/render src/app)
  target_compile_definitions(restore_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(restore_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(restore_ui_tests restore_ui_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(restore_ui_tests restore_ui_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(restore_ui_tests "restore_ui_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(restore_ui_tests)
  add_dependencies(restore_ui_tests glancelot-decoder)
  add_test(NAME restore_ui COMMAND restore_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/restore_ui-results.txt,txt" -o -,txt)
  set_tests_properties(restore_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 300)
  glancelot_test_labels(restore_ui LABELS ui process)
endif()
