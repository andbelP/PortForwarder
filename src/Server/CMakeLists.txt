add_library(Server)
target_sources(Server
    PUBLIC
    FILE_SET HEADERS
    FILES Server.hpp
    PRIVATE
    Server.cpp
)

target_link_libraries(Server PUBLIC Boost::system PRIVATE Tunnel)
