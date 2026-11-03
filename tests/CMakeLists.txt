add_executable(portforwarder_tests portforwarder_tests.cpp)
target_link_libraries(portforwarder_tests PRIVATE Agent Server Tunnel)
target_compile_features(portforwarder_tests PRIVATE cxx_std_17)

# The executable and CTest use the same catalogue, so no case can silently go
# unregistered. Each invocation runs one case in its own process.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cases.inc")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cases.inc" test_cases REGEX "^PF_TEST\\(")
list(LENGTH test_cases test_count)
if(NOT test_count EQUAL 108)
    message(FATAL_ERROR "Expected 108 PortForwarder tests, found ${test_count}")
endif()
foreach(test_case IN LISTS test_cases)
    string(REGEX MATCH "^PF_TEST\\(([a-z0-9_]+)," unused "${test_case}")
    set(test_name "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "_.*" "" test_group "${test_name}")
    add_test(NAME "${test_name}" COMMAND portforwarder_tests "${test_name}")
    set_tests_properties("${test_name}" PROPERTIES
        TIMEOUT 15
        LABELS "${test_group}"
    )
endforeach()
