find_package(Boost CONFIG REQUIRED COMPONENTS system)

add_subdirectory(Tunnel)

target_link_libraries(Tunnel PUBLIC Boost::system)
