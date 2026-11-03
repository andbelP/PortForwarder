cmake_minimum_required(VERSION 3.16)

project(PortForwarder LANGUAGES CXX)

add_subdirectory(src)
add_subdirectory(bin)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
