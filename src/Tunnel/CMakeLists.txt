# Tunnel component as a simple library (no OBJECT libraries)

add_library(Tunnel)

target_sources(Tunnel
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tunnel.cpp
    PUBLIC
        FILE_SET HEADERS 
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/Tunnel.hpp
)

target_compile_features(Tunnel PUBLIC cxx_std_17)
