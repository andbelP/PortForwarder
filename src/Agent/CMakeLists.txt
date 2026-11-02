add_library(Agent)
target_sources(Agent
    PUBLIC 
    FILE_SET HEADERS
    FILES Agent.hpp
    PRIVATE
    Agent.cpp
)

target_link_libraries(Agent PUBLIC Boost::system PRIVATE Tunnel)
