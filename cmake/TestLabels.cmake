# EN-03: CTest labels and resource locks. The root CMakeLists.txt includes this file before
# any test is registered and calls glancelot_finalize_test_labels() at its very end.
#
# Labels (definitions in docs/TESTING.md):
#   ui         loads the shipped QML (ui/Main.qml) in a QQuickWindow
#   budget     asserts wall-clock budgets; can flake under load (adds RESOURCE_LOCK budget)
#   native     touches the real OS: desktop shell integration, trash, subst, cmd mklink, or a
#              window on the real platform plugin (not forced offscreen)
#   process    starts any child process, directly or through product code: glancelot-decoder
#              (ViewerController::open, ThumbnailService), a test helper, the app, Python, cmd,
#              subst. Does not remove a test from fast
#   clipboard  reaches the system clipboard, directly or through product code
#              (adds RESOURCE_LOCK clipboard)
#   fast       computed, never declared: every registered test without ui, budget or native.
#              An undeclared test is therefore fast.
#
# A feature cmake file labels its own tests next to add_test:
#   glancelot_test_labels(<test> [LABELS ui budget native process clipboard] [LOCKS budget clipboard])
# Configure fails only when a declaration names a test that is not registered, or uses an
# unknown label or lock.
include_guard(GLOBAL)

set(GLANCELOT_TEST_LABEL_NAMES ui budget native process clipboard)
set(GLANCELOT_TEST_LOCK_NAMES budget clipboard)

function(glancelot_test_labels test_name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LABELS;LOCKS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "glancelot_test_labels(${test_name}): unexpected arguments '${arg_UNPARSED_ARGUMENTS}'. Use LABELS and LOCKS.")
  endif()
  set(locks ${arg_LOCKS})
  foreach(label IN LISTS arg_LABELS)
    if(NOT label IN_LIST GLANCELOT_TEST_LABEL_NAMES)
      message(FATAL_ERROR "glancelot_test_labels(${test_name}): unknown label '${label}'. Allowed: ${GLANCELOT_TEST_LABEL_NAMES} (fast is computed).")
    endif()
    if(label IN_LIST GLANCELOT_TEST_LOCK_NAMES)
      list(APPEND locks ${label})
    endif()
  endforeach()
  foreach(lock IN LISTS arg_LOCKS)
    if(NOT lock IN_LIST GLANCELOT_TEST_LOCK_NAMES)
      message(FATAL_ERROR "glancelot_test_labels(${test_name}): unknown lock '${lock}'. Allowed: ${GLANCELOT_TEST_LOCK_NAMES}.")
    endif()
  endforeach()
  set_property(GLOBAL APPEND PROPERTY GLANCELOT_LABELLED_TESTS ${test_name})
  set_property(GLOBAL APPEND PROPERTY GLANCELOT_TEST_LABELS_${test_name} ${arg_LABELS})
  set_property(GLOBAL APPEND PROPERTY GLANCELOT_TEST_LOCKS_${test_name} ${locks})
endfunction()

function(glancelot_finalize_test_labels)
  get_property(registered DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY TESTS)
  # Labels are applied in the root directory only; refuse tests registered in a subdirectory.
  get_property(pending DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY SUBDIRECTORIES)
  while(pending)
    list(POP_FRONT pending dir)
    get_property(dir_tests DIRECTORY "${dir}" PROPERTY TESTS)
    if(dir_tests)
      message(FATAL_ERROR "cmake/TestLabels.cmake: tests in subdirectory ${dir} (${dir_tests}) are not supported; register tests in the root directory.")
    endif()
    get_property(children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND pending ${children})
  endwhile()

  get_property(declared GLOBAL PROPERTY GLANCELOT_LABELLED_TESTS)
  list(REMOVE_DUPLICATES declared)
  foreach(test_name IN LISTS declared)
    if(NOT test_name IN_LIST registered)
      message(FATAL_ERROR "glancelot_test_labels(${test_name} ...): '${test_name}' is not a registered test. Fix the name or remove the declaration.")
    endif()
    get_property(labels GLOBAL PROPERTY GLANCELOT_TEST_LABELS_${test_name})
    get_property(locks GLOBAL PROPERTY GLANCELOT_TEST_LOCKS_${test_name})
    list(REMOVE_DUPLICATES labels)
    list(REMOVE_DUPLICATES locks)
    if(labels)
      set_property(TEST ${test_name} APPEND PROPERTY LABELS ${labels})
    endif()
    if(locks)
      set_property(TEST ${test_name} APPEND PROPERTY RESOURCE_LOCK ${locks})
    endif()
  endforeach()

  foreach(test_name IN LISTS registered)
    get_property(labels TEST ${test_name} PROPERTY LABELS)
    set(is_fast TRUE)
    foreach(excluded IN ITEMS ui budget native)
      if(excluded IN_LIST labels)
        set(is_fast FALSE)
      endif()
    endforeach()
    if(is_fast)
      set_property(TEST ${test_name} APPEND PROPERTY LABELS fast)
    endif()
  endforeach()
endfunction()

# Existing tests, declared centrally (EN-03). New packets call glancelot_test_labels in their
# own cmake file instead of editing this block. Tests with no line here are fast.
# Core tests (registered with or without the viewer):
glancelot_test_labels(selection_export LABELS native process)   # cmd mklink /J fallback
glancelot_test_labels(directory_listing LABELS native process)  # cmd mklink /J
glancelot_test_labels(file_operations LABELS native process)    # std::system mklink /J
if(GLANCELOT_BUILD_VIEWER)
  # Real-QML tests (ui) that also open images (decoder) or run cmd mklink.
  glancelot_test_labels(folder_grid LABELS ui budget native process clipboard)
  glancelot_test_labels(input_ui LABELS ui budget process clipboard)
  glancelot_test_labels(pathbar_ui LABELS ui)
  glancelot_test_labels(fs_watch_ui LABELS ui)
  glancelot_test_labels(monitor_customization_ui LABELS ui process)
  # Timing budgets.
  glancelot_test_labels(fs_watch_budget LABELS budget process)
  glancelot_test_labels(fs_watch LABELS budget native process)
  glancelot_test_labels(cache LABELS budget process)
  glancelot_test_labels(window_slots LABELS budget native process)
  glancelot_test_labels(recovery_controller LABELS budget process)
  # Real OS shell, trash, subst or a native app window.
  glancelot_test_labels(platform_integration LABELS native process)
  glancelot_test_labels(file_operation_service LABELS native process)
  glancelot_test_labels(recovery_process LABELS native process)
  # Child processes only (still fast).
  glancelot_test_labels(viewer_acceptance LABELS process)
  glancelot_test_labels(thumbnails LABELS process)
  glancelot_test_labels(folder_tree LABELS process)
  glancelot_test_labels(filter LABELS process)
  glancelot_test_labels(io_scheduler LABELS process)
  glancelot_test_labels(copy_move LABELS process)
  glancelot_test_labels(recovery LABELS process)
  glancelot_test_labels(recovery_startup LABELS process)
  # Clipboard through MetadataModel::copySummary/copyGps/copyTag.
  glancelot_test_labels(metadata LABELS clipboard)
endif()
