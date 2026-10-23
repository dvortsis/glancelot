# EN-05: the viewer controller and the sources it needs in every executable that
# compiles it. The app target and each controller test consume this one list, so a
# new controller dependency is added here and nowhere else.
include_guard(GLOBAL)
set(GLANCELOT_VIEWER_SOURCES
  src/app/viewer_controller.cpp include/glancelot/viewer_controller.hpp
  src/app/decode_cache.cpp src/app/decode_cache.hpp
  src/app/settings_file.cpp include/glancelot/settings_file.hpp)
