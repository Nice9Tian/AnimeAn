cmake_minimum_required(VERSION 3.16)

project(AnimeAn VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ANIMEAN_WITH_PYTHON "Enable pybind11 support for embedding Python in AnimeAn." ON)
option(ANIMEAN_BUILD_PYTHON_MODULE "Build the optional AnimeAn Python extension module." OFF)

set(ANIMEAN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "AnimeAn project root directory.")
set(ANIMEAN_PYFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pyfile" CACHE PATH "Directory containing hello-world Python scripts.")
set(ANIMEAN_PYTHONBIND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pythonbind" CACHE PATH "Directory containing Python bindings and helper modules.")
set(ANIMEAN_OPENTOONZ_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/opentoonz_tools" CACHE PATH "Directory containing OpenToonz import helper modules.")

if(ANIMEAN_WITH_PYTHON OR ANIMEAN_BUILD_PYTHON_MODULE)
    set(ANIMEAN_PYTHON_HOME "${CMAKE_CURRENT_SOURCE_DIR}/tools/python312" CACHE PATH "Python runtime used by AnimeAn.")
    set(ANIMEAN_PYTHON_DIR_NAME "python312" CACHE STRING "Python runtime directory name used in deployed AnimeAn packages.")
    if(EXISTS "${ANIMEAN_PYTHON_HOME}")
        set(Python_ROOT_DIR "${ANIMEAN_PYTHON_HOME}")
        set(Python_EXECUTABLE "${ANIMEAN_PYTHON_HOME}/python.exe")
    endif()

    set(PYBIND11_FINDPYTHON ON)
    set(PYBIND11_TEST OFF CACHE BOOL "Build pybind11 tests" FORCE)
    add_subdirectory(external/pybind11)
endif()

include(CTest)
# One home for the scene-model / file-format core: the backend, the Python
# module and the C++ unit tests all link animean_core, which has no Qt.
add_subdirectory(algorithm/core)
add_subdirectory(backend)

if(BUILD_TESTING)
    add_executable(projectio_tests tests/projectio_tests.cpp)
    target_link_libraries(projectio_tests PRIVATE animean_core)
    add_test(NAME projectio_roundtrip COMMAND projectio_tests)

    add_executable(animemodel_tests tests/animemodel_tests.cpp)
    target_link_libraries(animemodel_tests PRIVATE animean_core)
    add_test(NAME animemodel_asset_gc COMMAND animemodel_tests)

    add_executable(strokefit_tests tests/strokefit_tests.cpp)
    target_link_libraries(strokefit_tests PRIVATE animean_core)
    add_test(NAME strokefit_zoom_invariance COMMAND strokefit_tests)
endif()

if(ANIMEAN_BUILD_PYTHON_MODULE)
    set(ANIMEAN_DEFAULT_PYTHON_BINDINGS "${ANIMEAN_PYTHONBIND_DIR}/python_bindings.cpp")
    if(NOT DEFINED ANIMEAN_PYTHON_BINDINGS OR ANIMEAN_PYTHON_BINDINGS STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/pyfile/python_bindings.cpp")
        set(ANIMEAN_PYTHON_BINDINGS "${ANIMEAN_DEFAULT_PYTHON_BINDINGS}" CACHE FILEPATH "Source file for the AnimeAn Python extension module." FORCE)
    else()
        set(ANIMEAN_PYTHON_BINDINGS "${ANIMEAN_PYTHON_BINDINGS}" CACHE FILEPATH "Source file for the AnimeAn Python extension module.")
    endif()

    if(NOT EXISTS "${ANIMEAN_PYTHON_BINDINGS}")
        message(FATAL_ERROR "ANIMEAN_BUILD_PYTHON_MODULE is ON, but ANIMEAN_PYTHON_BINDINGS does not exist: ${ANIMEAN_PYTHON_BINDINGS}")
    endif()

    pybind11_add_module(animean_python MODULE "${ANIMEAN_PYTHON_BINDINGS}")
    target_include_directories(animean_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/algorithm ${CMAKE_CURRENT_SOURCE_DIR}/pythonbind)
    target_compile_definitions(animean_python PRIVATE ANIMEAN_BUILDING_PYTHON_MODULE)
    target_link_libraries(animean_python PRIVATE animean_core)
endif()
