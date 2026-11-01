cmake_minimum_required(VERSION 3.16)

# Works two ways: add_subdirectory from the repository root, or configured on
# its own, which is much faster while working on the core alone:
#   cmake -S algorithm/core -B build/core -G Ninja
#         -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/mingw_64 -DBUILD_TESTING=ON
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(animean_corelib LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(CTest)
endif()

get_filename_component(ANIMEAN_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(animean_corelib STATIC
    bridge.h
    color.h
    containers.h
    geometry.h
    image.cpp
    image.h
    json.cpp
    json.h
    painter.cpp
    painter.h
    path.cpp
    path.h
    pathbool.cpp
    pathbool.h
    pathops.cpp
    pathops.h
    string.cpp
    string.h
    ${ANIMEAN_CORE_ROOT}/external/clipper2/src/clipper.engine.cpp
    ${ANIMEAN_CORE_ROOT}/external/clipper2/src/clipper.offset.cpp
    ${ANIMEAN_CORE_ROOT}/external/clipper2/src/clipper.rectclip.cpp
)

target_include_directories(animean_corelib
    PUBLIC
        ${ANIMEAN_CORE_ROOT}
    PRIVATE
        ${ANIMEAN_CORE_ROOT}/external/clipper2/include
        ${ANIMEAN_CORE_ROOT}/external/stb
)

# The computation layer. It has no Qt on its include path either, which is what
# keeps it portable; the Qt program converts at the boundary (qtbridge.h).
add_library(animean_core STATIC
    ${ANIMEAN_CORE_ROOT}/algorithm/animemodel.cpp
    ${ANIMEAN_CORE_ROOT}/algorithm/animemodel.h
    ${ANIMEAN_CORE_ROOT}/algorithm/beziersplit.cpp
    ${ANIMEAN_CORE_ROOT}/algorithm/beziersplit.h
    ${ANIMEAN_CORE_ROOT}/algorithm/scenehistory.cpp
    ${ANIMEAN_CORE_ROOT}/algorithm/scenehistory.h
    ${ANIMEAN_CORE_ROOT}/algorithm/vectorlogic.cpp
    ${ANIMEAN_CORE_ROOT}/algorithm/vectorlogic.h
    ${ANIMEAN_CORE_ROOT}/algorithm/viewscale.h
    ${ANIMEAN_CORE_ROOT}/clipreader.cpp
    ${ANIMEAN_CORE_ROOT}/clipreader.h
    ${ANIMEAN_CORE_ROOT}/projectio.cpp
    ${ANIMEAN_CORE_ROOT}/projectio.h
)
target_include_directories(animean_core PUBLIC
    ${ANIMEAN_CORE_ROOT}
    ${ANIMEAN_CORE_ROOT}/algorithm
)
target_link_libraries(animean_core PUBLIC animean_corelib)

# Differential tests: each one links the real Qt type and checks the core type
# gives the same answer. The tests may use Qt; the library above must not, and
# since it has no Qt on its include path, a stray include fails the build.
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    foreach(ANIMEAN_CORE_TEST geometry containers string json path pathops image)
        set(ANIMEAN_CORE_TEST_SOURCE ${ANIMEAN_CORE_ROOT}/tests/core/${ANIMEAN_CORE_TEST}_tests.cpp)
        if(EXISTS ${ANIMEAN_CORE_TEST_SOURCE})
            add_executable(core_${ANIMEAN_CORE_TEST}_tests ${ANIMEAN_CORE_TEST_SOURCE})
            target_link_libraries(core_${ANIMEAN_CORE_TEST}_tests PRIVATE animean_corelib Qt6::Core Qt6::Gui)
            add_test(NAME core_${ANIMEAN_CORE_TEST} COMMAND core_${ANIMEAN_CORE_TEST}_tests)
            set_property(TEST core_${ANIMEAN_CORE_TEST} PROPERTY ENVIRONMENT
                "PATH=${Qt6_DIR}/../../../bin\;$ENV{PATH}"
                "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()
endif()
