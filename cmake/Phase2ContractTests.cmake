# PHASE2-CONTRACTS: the frozen headers have no implementation yet, so nothing else would compile them.
# - One generated source per header, including that header first, proves each is self-contained.
# - contract_headers_tests checks the frozen values and that the new SortKey values stay unreachable and harmless.
# - contract_headers_moc runs moc over the QObject headers and compiles the result without linking it, because the
#   classes have no definitions until packets C3, D2 and E1. It belongs to the "all" target, so a bad Q_PROPERTY fails the build.
# A packet that implements a header removes it from these lists if it starts compiling the header itself.
set(glancelot_contract_headers
  asset_group capability_model compare_panes contracts navigation operation_journal resource_ledger)
set(glancelot_contract_units)
foreach(header IN LISTS glancelot_contract_headers)
  set(unit "${CMAKE_CURRENT_BINARY_DIR}/contract_headers/${header}_selfcontained.cpp")
  file(CONFIGURE OUTPUT "${unit}" CONTENT "#include \"glancelot/${header}.hpp\"\n" @ONLY)
  list(APPEND glancelot_contract_units "${unit}")
endforeach()
qt_add_executable(contract_headers_tests tests/contract_headers_tests.cpp ${glancelot_contract_units})
target_include_directories(contract_headers_tests PRIVATE include)
target_link_libraries(contract_headers_tests PRIVATE glancelot_core Qt6::Core Qt6::Gui Threads::Threads)
add_test(NAME contract_headers COMMAND contract_headers_tests)
set_tests_properties(contract_headers PROPERTIES TIMEOUT 60)

qt_wrap_cpp(glancelot_contract_moc include/glancelot/capability_model.hpp include/glancelot/compare_panes.hpp
  include/glancelot/resource_ledger.hpp)
add_library(contract_headers_moc OBJECT ${glancelot_contract_moc})
set_target_properties(contract_headers_moc PROPERTIES AUTOMOC OFF)
target_include_directories(contract_headers_moc PRIVATE include)
target_link_libraries(contract_headers_moc PRIVATE Qt6::Core Qt6::Gui)
