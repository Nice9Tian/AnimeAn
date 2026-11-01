cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(animean_net VERSION 0.1 LANGUAGES CXX)
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(ANIMEAN_NET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT TARGET animean_platform)
    add_subdirectory("${ANIMEAN_NET_ROOT}/backend/platform" "${CMAKE_CURRENT_BINARY_DIR}/platform")
endif()

add_library(animean_ws STATIC
    wsserver.cpp
    wsserver.h
)

target_include_directories(animean_ws PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(animean_ws PUBLIC
    animean_platform
    ws2_32
)

add_executable(ws_echo
    tests/ws_echo.cpp
)

target_link_libraries(ws_echo PRIVATE
    animean_ws
)
