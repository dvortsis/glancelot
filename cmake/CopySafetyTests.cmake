# COPY-SAFETY-01 review round 1: same-file aliases (subst, junctions, symbolic and hard links), the replaced
# file keeping the target's attributes, permissions and streams, and long paths. Runs cmd mklink /J and subst.
qt_add_executable(copy_safety_tests tests/copy_safety_tests.cpp tests/copy_safety_test_support.hpp)
target_include_directories(copy_safety_tests PRIVATE include src/app tests)
target_link_libraries(copy_safety_tests PRIVATE glancelot_core glancelot_platform Qt6::Test)
add_test(NAME copy_safety COMMAND copy_safety_tests -o "${CMAKE_CURRENT_BINARY_DIR}/copy_safety-results.txt,txt" -o -,txt)
set_tests_properties(copy_safety PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
glancelot_test_labels(copy_safety LABELS native process)
