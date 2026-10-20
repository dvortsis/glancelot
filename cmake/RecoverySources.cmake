# SAFE-MODE-01 lead-frozen production wiring. Include after glancelot exists,
# outside BUILD_TESTING, when real feature sources are present.
include_guard(GLOBAL)
set(glancelot_recovery_implementation_sources
  src/app/settings_recovery.cpp src/app/settings_policy.cpp src/app/settings_diff.cpp
  src/app/settings_restore.cpp src/app/recovery_checkpoint.cpp src/app/recovery_session.cpp
  src/app/command_registry.cpp app/settings_maintenance.cpp)
set(glancelot_recovery_provider_sources src/app/window_slot.cpp src/app/settings_file.cpp)
add_library(glancelot_recovery_common STATIC ${glancelot_recovery_implementation_sources})
target_include_directories(glancelot_recovery_common PUBLIC include PRIVATE src/app)
target_link_libraries(glancelot_recovery_common PUBLIC Qt6::Core Qt6::Gui Threads::Threads)

function(glancelot_attach_recovery recovery_target)
  get_target_property(recovery_already_attached ${recovery_target} GLANCELOT_RECOVERY_ATTACHED)
  if(recovery_already_attached)
    return()
  endif()
  get_target_property(recovery_existing_sources ${recovery_target} SOURCES)
  foreach(recovery_source IN LISTS glancelot_recovery_provider_sources)
    list(FIND recovery_existing_sources "${recovery_source}" recovery_source_index)
    if(recovery_source_index EQUAL -1)
      target_sources(${recovery_target} PRIVATE "${recovery_source}")
    endif()
  endforeach()
  # Dedicated feature executables compile their own real engine with test hooks.
  if(NOT recovery_target STREQUAL "recovery_tests" AND
     NOT recovery_target STREQUAL "recovery_controller_tests")
    target_link_libraries(${recovery_target} PRIVATE glancelot_recovery_common)
  endif()
  set_property(TARGET ${recovery_target} PROPERTY GLANCELOT_RECOVERY_ATTACHED TRUE)
endfunction()
glancelot_attach_recovery(glancelot)
target_sources(glancelot PRIVATE app/startup_intent.cpp)
