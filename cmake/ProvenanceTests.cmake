# REL-01: build provenance tests. build_info: the generated values, --version from the shipped executable, the BuildInfo QML
# singleton, the About rows and their catalog entries. provenance_configure: the configure-time rules on a scratch project.
# docker_build_args: scripts/docker_build_args.py against the Dockerfile.
if(BUILD_TESTING)
  qt_add_executable(build_info_ui_tests tests/build_info_ui_tests.cpp
    src/app/theme_manager.cpp include/glancelot/theme_manager.hpp
    src/app/settings_file.cpp include/glancelot/settings_file.hpp)
  target_include_directories(build_info_ui_tests PRIVATE include src/app)
  target_link_libraries(build_info_ui_tests PRIVATE glancelot_build_info Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  target_compile_definitions(build_info_ui_tests PRIVATE
    GLANCELOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GLANCELOT_TEST_PROJECT_VERSION="${PROJECT_VERSION}"
    GLANCELOT_TEST_VERSION_FULL="${GLANCELOT_VERSION_FULL}"
    GLANCELOT_TEST_KIND="${GLANCELOT_BUILD_KIND_RESOLVED}"
    GLANCELOT_TEST_CONFIGURED_SHA="${GLANCELOT_GIT_SHA_RESOLVED}"
    GLANCELOT_TEST_APP="$<TARGET_FILE:glancelot>")
  qt_add_resources(build_info_ui_tests build_info_theme_catalog PREFIX "/" BASE assets FILES assets/themes/catalog.json)
  add_dependencies(build_info_ui_tests glancelot)
  add_test(NAME build_info COMMAND build_info_ui_tests -o "${CMAKE_CURRENT_BINARY_DIR}/build_info-results.txt,txt" -o -,txt)
  set_tests_properties(build_info PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
  glancelot_test_labels(build_info LABELS ui process)

  set(provenance_git "${GIT_EXECUTABLE}")
  if(provenance_git MATCHES "-NOTFOUND$")
    set(provenance_git "")
  endif()
  add_test(NAME provenance_configure COMMAND "${CMAKE_COMMAND}"
    "-DGLANCELOT_REPO=${CMAKE_CURRENT_SOURCE_DIR}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/provenance_configure"
    "-DGENERATOR=${CMAKE_GENERATOR}" "-DGIT=${provenance_git}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/provenance/run_configure_tests.cmake")
  set_tests_properties(provenance_configure PROPERTIES TIMEOUT 300)
  glancelot_test_labels(provenance_configure LABELS process)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME docker_build_args COMMAND "${Python3_EXECUTABLE}" -I
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/provenance/docker_build_args_test.py" "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(docker_build_args PROPERTIES TIMEOUT 60)
    glancelot_test_labels(docker_build_args LABELS process)
  endif()
endif()
