# EN-03: CTest labels and resource locks for every registered test. Included once
# from the root after all tests exist. The categories are documented in docs/TESTING.md:
#   fast       every test that is not ui, budget or native (configure checks this exactly)
#   ui         loads the shipped QML (ui/Main.qml) in a QQuickWindow
#   budget     asserts wall-clock budgets; can flake under load (RESOURCE_LOCK budget)
#   native     touches the real OS: desktop shell integration, trash, subst, cmd mklink, or a
#              window on the real platform plugin (not forced offscreen)
#   process    the test starts a hermetic child process (decoder, test helper, the app or
#              Python); may overlap fast. A cmd/subst/explorer child alone does not count
#   clipboard  reaches the system clipboard, directly or through product code
#              (RESOURCE_LOCK clipboard)
# A new test must be added to at least one list, and to fast unless it is ui, budget or
# native. A name that is not a registered test fails configure.
include_guard(GLOBAL)
set(glancelot_tests_fast
  perf_trace navigation startup_window_geometry viewer_acceptance thumbnails folder_tree pane_layout
  directory_model viewport themes grid_search grid_density ui_settings favorites folder_history
  shortcuts_help filter io_scheduler metadata animation mutation copy_move format monitor_output
  recovery recovery_startup monitor_customization)
set(glancelot_tests_ui folder_grid input_ui pathbar_ui fs_watch_ui monitor_customization_ui)
set(glancelot_tests_budget fs_watch_budget fs_watch folder_grid cache window_slots recovery_controller input_ui)
set(glancelot_tests_native
  platform_integration file_operation_service recovery_process folder_grid window_slots fs_watch
  file_operations directory_listing selection_export)
set(glancelot_tests_process
  thumbnails io_scheduler fs_watch window_slots recovery recovery_startup platform_integration recovery_process)
set(glancelot_tests_clipboard folder_grid input_ui metadata)
# Tests registered without the viewer (GLANCELOT_BUILD_VIEWER=OFF).
set(glancelot_tests_core selection_export perf_trace navigation directory_listing file_operations)
set(glancelot_test_lists fast ui budget native process clipboard)

get_property(glancelot_registered_tests DIRECTORY PROPERTY TESTS)

# Every listed name must be a registered test. With the viewer off, viewer test names are
# dropped, but core test names are still checked.
foreach(test_name IN LISTS glancelot_tests_core)
  if(NOT test_name IN_LIST glancelot_registered_tests)
    message(FATAL_ERROR "cmake/TestLabels.cmake: '${test_name}' in the core list is not a registered test. Fix the name or remove it.")
  endif()
endforeach()
foreach(test_list IN LISTS glancelot_test_lists)
  foreach(test_name IN LISTS glancelot_tests_${test_list})
    if(NOT test_name IN_LIST glancelot_registered_tests)
      if(GLANCELOT_BUILD_VIEWER OR test_name IN_LIST glancelot_tests_core)
        message(FATAL_ERROR "cmake/TestLabels.cmake: '${test_name}' in the ${test_list} list is not a registered test. Fix the name or remove it.")
      endif()
      list(REMOVE_ITEM glancelot_tests_${test_list} ${test_name})
    endif()
  endforeach()
endforeach()

foreach(test_list IN LISTS glancelot_test_lists)
  foreach(test_name IN LISTS glancelot_tests_${test_list})
    set_property(TEST ${test_name} APPEND PROPERTY LABELS ${test_list})
  endforeach()
endforeach()
foreach(test_lock IN ITEMS budget clipboard)
  foreach(test_name IN LISTS glancelot_tests_${test_lock})
    set_property(TEST ${test_name} APPEND PROPERTY RESOURCE_LOCK ${test_lock})
  endforeach()
endforeach()

foreach(test_name IN LISTS glancelot_registered_tests)
  get_property(test_labels TEST ${test_name} PROPERTY LABELS)
  if(NOT test_labels)
    message(FATAL_ERROR "Test '${test_name}' has no CTest label. Add it to a list in cmake/TestLabels.cmake (see docs/TESTING.md).")
  endif()
  # fast is exactly the tests that are not ui, budget or native.
  set(test_expect_fast TRUE)
  foreach(test_list IN ITEMS ui budget native)
    if(test_name IN_LIST glancelot_tests_${test_list})
      set(test_expect_fast FALSE)
    endif()
  endforeach()
  if(test_name IN_LIST glancelot_tests_fast)
    set(test_is_fast TRUE)
  else()
    set(test_is_fast FALSE)
  endif()
  if(test_expect_fast AND NOT test_is_fast)
    message(FATAL_ERROR "Test '${test_name}' is not ui, budget or native, so it must be in the fast list in cmake/TestLabels.cmake (see docs/TESTING.md).")
  elseif(test_is_fast AND NOT test_expect_fast)
    message(FATAL_ERROR "Test '${test_name}' is ui, budget or native, so it must not be in the fast list in cmake/TestLabels.cmake (see docs/TESTING.md).")
  endif()
endforeach()
