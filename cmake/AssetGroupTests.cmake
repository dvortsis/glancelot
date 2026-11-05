# B1 ASSET-GROUP. This file also adds the production source, so one include line in the root CMakeLists.txt is the whole
# root change: asset_group.cpp joins glancelot_platform as the contract says, and glancelot_platform now needs the core
# (snapshotTarget), which it did not before. Include it after every target below exists.
target_sources(glancelot_platform PRIVATE src/app/asset_group.cpp include/glancelot/asset_group.hpp src/app/asset_group_internal.hpp)
target_link_libraries(glancelot_platform PUBLIC glancelot_core)
# The confirmations' components, in the module root CMakeLists.txt declares. The UI test targets load ui/ from source.
qt_target_qml_sources(glancelot QML_FILES ui/AssetCompanions.qml ui/TransferFlow.qml ui/UiRadioButton.qml)
if(BUILD_TESTING)
  # FileOperationService now expands through glancelot_platform.
  target_link_libraries(file_operation_service_tests PRIVATE glancelot_platform)
  qt_add_executable(asset_group_tests tests/asset_group_tests.cpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp src/app/platform_trash.hpp)
  target_include_directories(asset_group_tests PRIVATE include src/app)
  target_link_libraries(asset_group_tests PRIVATE glancelot_core glancelot_platform Qt6::Core Qt6::Test Qt6::Concurrent)
  add_test(NAME asset_group COMMAND asset_group_tests -o "${CMAKE_CURRENT_BINARY_DIR}/asset_group-results.txt,txt" -o -,txt)
  set_tests_properties(asset_group PROPERTIES TIMEOUT 120)
  # The confirmations through the shipped Main.qml. Sources and environment mirror reference_pin.
  qt_add_executable(asset_group_ui_tests tests/asset_group_ui_tests.cpp
    ${GLANCELOT_VIEWER_SOURCES}
    ${GLANCELOT_VIEWER_CONTEXT_SOURCES}
    src/app/pane_layout.cpp include/glancelot/pane_layout.hpp
    src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
    src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp
    src/app/file_operation_service.cpp include/glancelot/file_operation_service.hpp
    src/render/image_canvas.cpp src/render/image_canvas.hpp)
  target_include_directories(asset_group_ui_tests PRIVATE include src/app src/render)
  target_compile_definitions(asset_group_ui_tests PRIVATE GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(asset_group_ui_tests PRIVATE glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent)
  qt_add_resources(asset_group_ui_tests asset_group_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json ${ui_fonts})
  qt_add_resources(asset_group_ui_tests asset_group_brand_assets PREFIX "/" BASE assets FILES
    assets/brand/icons/app/dark/glancelot-64.png assets/brand/icons/app/light/glancelot-64.png
    assets/brand/mascots/transparent/01_mascot_standing.png)
  qt_add_shaders(asset_group_ui_tests "asset_group_shaders" PREFIX "/shaders" FILES src/render/image.vert src/render/image.frag)
  glancelot_attach_monitor_customization(asset_group_ui_tests)
  add_dependencies(asset_group_ui_tests glancelot-decoder)
  add_test(NAME asset_group_ui COMMAND asset_group_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/asset_group_ui-results.txt,txt" -o -,txt)
  set_tests_properties(asset_group_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
  glancelot_test_labels(asset_group_ui LABELS ui process)
endif()
