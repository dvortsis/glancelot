# Lead-frozen FS-WATCH-01 test target. Included by the lead on the feature
# branch once tests/fs_watch_tests.cpp exists; required before candidate review.
qt_add_executable(fs_watch_tests tests/fs_watch_tests.cpp
  src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
  src/app/settings_file.cpp include/glancelot/settings_file.hpp
  src/app/directory_entry_model.cpp include/glancelot/directory_entry_model.hpp
  src/app/filmstrip_model.cpp include/glancelot/filmstrip_model.hpp
  src/app/folder_tree_model.cpp include/glancelot/folder_tree_model.hpp)
target_include_directories(fs_watch_tests PRIVATE include src/app)
target_link_libraries(fs_watch_tests PRIVATE glancelot_core glancelot_platform
  Qt6::Gui Qt6::Test Qt6::Concurrent)
add_dependencies(fs_watch_tests glancelot-decoder)
add_test(NAME fs_watch COMMAND fs_watch_tests -o -,txt)
set_tests_properties(fs_watch PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
