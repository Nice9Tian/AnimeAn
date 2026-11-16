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
    # The Python version a release ships, and the one the wheels in pywheels/
    # are built for.
    set(ANIMEAN_PYTHON_VERSION_MAJOR 3)
    set(ANIMEAN_PYTHON_VERSION_MINOR 12)

    # The Python runtime AnimeAn embeds: a full Python of that version with
    # headers and its library. On the Windows development machine it is
    # tools/python312 inside the checkout. A checkout without that folder (a
    # worktree, a Linux machine) names one with -DANIMEAN_PYTHON_HOME=<folder>
    # or the environment variable ANIMEAN_PYTHON_HOME.
    set(ANIMEAN_DEFAULT_PYTHON_HOME "${CMAKE_CURRENT_SOURCE_DIR}/tools/python312")
    if(NOT EXISTS "${ANIMEAN_DEFAULT_PYTHON_HOME}" AND DEFINED ENV{ANIMEAN_PYTHON_HOME})
        file(TO_CMAKE_PATH "$ENV{ANIMEAN_PYTHON_HOME}" ANIMEAN_DEFAULT_PYTHON_HOME)
    endif()
    set(ANIMEAN_PYTHON_HOME "${ANIMEAN_DEFAULT_PYTHON_HOME}" CACHE PATH "Python runtime used by AnimeAn.")
    set(ANIMEAN_PYTHON_DIR_NAME "python312" CACHE STRING "Python runtime directory name used in deployed AnimeAn packages.")
    if(EXISTS "${ANIMEAN_PYTHON_HOME}")
        set(Python_ROOT_DIR "${ANIMEAN_PYTHON_HOME}")
        if(CMAKE_CROSSCOMPILING)
            # Building the Windows programs on another system
            # (build_scripts/windows_cross_check.sh): the runtime's own
            # interpreter cannot run here and is not asked. Headers and library
            # are named directly, and the version is read from the headers.
            # Only the programs that embed Python are meant to be built this
            # way; the optional extension module would get its file suffix from
            # whatever Python this machine has.
            set(Python_INCLUDE_DIR "${ANIMEAN_PYTHON_HOME}/include")
            set(Python_LIBRARY "${ANIMEAN_PYTHON_HOME}/libs/python${ANIMEAN_PYTHON_VERSION_MAJOR}${ANIMEAN_PYTHON_VERSION_MINOR}.lib")
        elseif(WIN32)
            set(Python_EXECUTABLE "${ANIMEAN_PYTHON_HOME}/python.exe")
        else()
            set(Python_EXECUTABLE "${ANIMEAN_PYTHON_HOME}/bin/python3")
        endif()
    else()
        # The build then takes whatever Python CMake finds, which is enough to
        # check that the code compiles. The programs look for their runtime in
        # ANIMEAN_PYTHON_HOME when they start, and will not start without it.
        message(WARNING "ANIMEAN_PYTHON_HOME (${ANIMEAN_PYTHON_HOME}) does not exist: "
                        "the programs built here will compile but will not find a Python runtime when run.")
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

    add_executable(fileio_tests tests/fileio_tests.cpp)
    target_link_libraries(fileio_tests PRIVATE animean_corelib)
    add_test(NAME fileio COMMAND fileio_tests)
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
