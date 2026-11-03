add_executable(agent agent.cpp)
target_link_libraries(agent PRIVATE Agent)
target_compile_features(agent PRIVATE cxx_std_17)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE Server)
target_compile_features(server PRIVATE cxx_std_17)
