# EN-03: CTest labels and resource locks for every registered test. Included once
# from the root after all tests exist. The categories are documented in docs/TESTING.md:
#   fast       quick loop: everything not labelled ui or budget, except recovery_process
#   ui         loads the shipped QML (ui/Main.qml) in a real QQuickWindow
#   budget     asserts wall-clock budgets; can flake under load (RESOURCE_LOCK budget)
#   process    the test itself starts a child process (decoder, helper, app or Python)
#   native     depends on platform shell, trash, screens or a real app window
#   clipboard  reads or writes the system clipboard (RESOURCE_LOCK clipboard)
# A new test must be added to at least one list; configure fails otherwise.
include_guard(GLOBAL)
set(glancelot_tests_fast
  selection_export perf_trace navigation directory_listing file_operations startup_window_geometry
  viewer_acceptance thumbnails folder_tree pane_layout file_operation_service directory_model viewport
  themes platform_integration grid_search grid_density ui_settings favorites folder_history
  shortcuts_help filter io_scheduler metadata animation mutation copy_move format monitor_output
  recovery recovery_startup monitor_customization)
set(glancelot_tests_ui folder_grid input_ui pathbar_ui fs_watch_ui monitor_customization_ui)
set(glancelot_tests_budget fs_watch_budget fs_watch folder_grid cache window_slots recovery_controller input_ui)
set(glancelot_tests_process
  thumbnails io_scheduler fs_watch window_slots recovery recovery_startup platform_integration recovery_process)
set(glancelot_tests_native
  platform_integration file_operation_service monitor_output monitor_customization monitor_customization_ui
  recovery_process)
set(glancelot_tests_clipboard folder_grid input_ui)

function(glancelot_label_tests label)
  foreach(test_name IN LISTS ARGN)
    # Viewer tests do not exist when GLANCELOT_BUILD_VIEWER is OFF.
    if(TEST ${test_name})
      set_property(TEST ${test_name} APPEND PROPERTY LABELS ${label})
    endif()
  endforeach()
endfunction()
foreach(test_label IN ITEMS fast ui budget process native clipboard)
  glancelot_label_tests(${test_label} ${glancelot_tests_${test_label}})
endforeach()
foreach(test_lock IN ITEMS budget clipboard)
  foreach(test_name IN LISTS glancelot_tests_${test_lock})
    if(TEST ${test_name})
      set_property(TEST ${test_name} APPEND PROPERTY RESOURCE_LOCK ${test_lock})
    endif()
  endforeach()
endforeach()

get_property(glancelot_registered_tests DIRECTORY PROPERTY TESTS)
foreach(test_name IN LISTS glancelot_registered_tests)
  get_property(test_labels TEST ${test_name} PROPERTY LABELS)
  if(NOT test_labels)
    message(FATAL_ERROR "Test '${test_name}' has no CTest label. Add it to a list in cmake/TestLabels.cmake (see docs/TESTING.md).")
  endif()
endforeach()
