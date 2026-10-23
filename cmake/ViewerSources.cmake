# EN-05: the viewer controller and the sources it needs in every executable that
# compiles it. The app target and each controller test consume this one list, so a
# new controller dependency is added here and nowhere else.
include_guard(GLOBAL)
set(GLANCELOT_VIEWER_SOURCES
  src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
  src/app/decode_cache.cpp src/app/decode_cache.hpp
  src/app/settings_file.cpp include/glancelot/settings_file.hpp)
# EN-04: the context services src/app/viewer_context.hpp installs beyond the controller.
# Every target that loads the shipped ui/Main.qml through that helper compiles this list
# (input_ui, pathbar_ui, grid_ui, fs_watch_ui, monitor_customization_ui, command_routing).
# Headers are listed for AUTOMOC; favorites_model.cpp needs settings_file.cpp from the list above.
set(GLANCELOT_VIEWER_CONTEXT_SOURCES
  src/app/favorites_model.cpp include/glancelot/favorites_model.hpp
  src/app/folder_history.cpp include/glancelot/folder_history.hpp)
