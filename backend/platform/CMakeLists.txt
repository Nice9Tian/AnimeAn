cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(animean_platform VERSION 0.1 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(CTest)
endif()

set(CMAKE_AUTOMOC OFF)

get_filename_component(ANIMEAN_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT TARGET animean_corelib)
    add_subdirectory("${ANIMEAN_PLATFORM_ROOT}/algorithm/core" "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

add_library(animean_platform STATIC
    elapsedtimer.h
    eventloop.cpp
    eventloop.h
    signalslot.h
    timer.cpp
    timer.h
)

target_include_directories(animean_platform PUBLIC
    ${ANIMEAN_PLATFORM_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(animean_platform PUBLIC
    animean_corelib
    ws2_32
)

if(BUILD_TESTING)
    add_executable(platform_tests
        tests/platform_tests.cpp
    )
    target_link_libraries(platform_tests PRIVATE
        animean_platform
    )
    add_test(NAME platform_tests COMMAND platform_tests)
endif()
