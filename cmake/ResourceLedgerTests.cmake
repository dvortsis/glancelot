# E1 (RESOURCE-LEDGER, docs/contracts/RESOURCE-LEDGER.md): the ledger's acceptance tests.
# The ledger itself is compiled into glancelot_platform (root CMakeLists.txt, E1 block).
#
# cmake/Phase2ContractTests.cmake runs qt_wrap_cpp over the frozen header for its unlinked moc check, and qt_wrap_cpp
# marks its input SKIP_AUTOMOC, which would leave glancelot_platform without the ledger's moc output. Source properties
# are read at generate time, so clearing it here (after that file) restores AUTOMOC for the real implementation.
set_source_files_properties(include/glancelot/resource_ledger.hpp PROPERTIES SKIP_AUTOMOC OFF)
if(BUILD_TESTING)
  qt_add_executable(resource_ledger_tests tests/resource_ledger_tests.cpp)
  target_include_directories(resource_ledger_tests PRIVATE include src/app src/render)
  target_link_libraries(resource_ledger_tests PRIVATE glancelot_core glancelot_platform Qt6::Gui Qt6::Test)
  add_test(NAME resource_ledger COMMAND resource_ledger_tests -o "${CMAKE_CURRENT_BINARY_DIR}/resource_ledger-results.txt,txt" -o -,txt)
  set_tests_properties(resource_ledger PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  # CanvasTextureAdmission (src/render/canvas_texture_admission.hpp) now holds ledger reservations, so the one
  # image_canvas target that did not link the platform library needs it.
  if(TARGET canvas_tests)
    target_link_libraries(canvas_tests PRIVATE glancelot_platform)
  endif()
endif()
