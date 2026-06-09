cmake_minimum_required(VERSION 3.16)

project(AnimeAn VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGLWidgets)

set(PROJECT_SOURCES
    algorithm/animemodel.cpp
    algorithm/animemodel.h
    algorithm/vectorlogic.cpp
    algorithm/vectorlogic.h
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    projectio.cpp
    projectio.h
    selectionattention.cpp
    selectionattention.h
    openglwidget.cpp
    openglwidget.h
    childrenpanel/assetpanel.cpp
    childrenpanel/assetpanel.h
    childrenpanel/assetpanel.ui
    childrenpanel/framepanel.cpp
    childrenpanel/framepanel.h
    childrenpanel/framepanel.ui
    childrenpanel/layerpanel.cpp
    childrenpanel/layerpanel.h
    childrenpanel/layerpanel.ui
    childrenpanel/tooloptpanel.cpp
    childrenpanel/tooloptpanel.h
    childrenpanel/tooloptpanel.ui
    childrenpanel/toolspanel.cpp
    childrenpanel/toolspanel.h
    childrenpanel/toolspanel.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(AnimeAn
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(AnimeAn SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(AnimeAn
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_include_directories(AnimeAn PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/algorithm
    ${CMAKE_CURRENT_SOURCE_DIR}/pythonbind
)

target_link_libraries(AnimeAn PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGLWidgets)

if(ANIMEAN_WITH_PYTHON)
    target_sources(AnimeAn PRIVATE pythonbind/python_bindings.cpp pythonbind/python_bindings.h)
    target_link_libraries(AnimeAn PRIVATE pybind11::embed)
    target_compile_definitions(AnimeAn PRIVATE
        ANIMEAN_WITH_PYTHON
        ANIMEAN_SOURCE_DIR="${ANIMEAN_SOURCE_DIR}"
        ANIMEAN_PYFILE_DIR="${ANIMEAN_PYFILE_DIR}"
        ANIMEAN_PYTHONBIND_DIR="${ANIMEAN_PYTHONBIND_DIR}"
        ANIMEAN_OPENTOONZ_TOOLS_DIR="${ANIMEAN_OPENTOONZ_TOOLS_DIR}"
        ANIMEAN_PYTHON_HOME="${ANIMEAN_PYTHON_HOME}"
        ANIMEAN_PYTHON_DIR_NAME="${ANIMEAN_PYTHON_DIR_NAME}"
    )

    foreach(ANIMEAN_PYTHON_DLL python312.dll python3.dll vcruntime140.dll vcruntime140_1.dll)
        if(EXISTS "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}")
            add_custom_command(TARGET AnimeAn POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}"
                    "$<TARGET_FILE_DIR:AnimeAn>"
            )
        endif()
    endforeach()

    add_custom_command(TARGET AnimeAn POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_PYFILE_DIR}/hello_world.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_PYFILE_DIR}/toolcontrol.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_PYFILE_DIR}/linefinish.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_PYFILE_DIR}/bind_test.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_PYTHONBIND_DIR}/animemodel.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANIMEAN_OPENTOONZ_TOOLS_DIR}/toonz_to_dict.py"
            "$<TARGET_FILE_DIR:AnimeAn>"
    )
endif()

if(WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        set(ANIMEAN_DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/AnimeAn")
        set(ANIMEAN_EXE_NAME "AnimeAn${CMAKE_EXECUTABLE_SUFFIX}")
        set(ANIMEAN_BUILT_EXE "${CMAKE_CURRENT_BINARY_DIR}/${ANIMEAN_EXE_NAME}")
        set(ANIMEAN_DEPLOYED_EXE "${ANIMEAN_DEPLOY_DIR}/${ANIMEAN_EXE_NAME}")
        set(ANIMEAN_DEPLOY_PYTHON_DLL_COMMANDS)
        if(ANIMEAN_WITH_PYTHON)
            foreach(ANIMEAN_PYTHON_DLL python312.dll python3.dll vcruntime140.dll vcruntime140_1.dll)
                if(EXISTS "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}")
                    list(APPEND ANIMEAN_DEPLOY_PYTHON_DLL_COMMANDS
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}"
                            "${ANIMEAN_DEPLOY_DIR}"
                    )
                endif()
            endforeach()
        endif()

        add_custom_target(deploy_AnimeAn
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_BUILT_EXE}"
                "${ANIMEAN_DEPLOY_DIR}"
            ${ANIMEAN_DEPLOY_PYTHON_DLL_COMMANDS}
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --dir "${ANIMEAN_DEPLOY_DIR}"
                "${ANIMEAN_DEPLOYED_EXE}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_PYFILE_DIR}/hello_world.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_PYFILE_DIR}/toolcontrol.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_PYFILE_DIR}/linefinish.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_PYFILE_DIR}/bind_test.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_PYTHONBIND_DIR}/animemodel.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ANIMEAN_OPENTOONZ_TOOLS_DIR}/toonz_to_dict.py"
                "${ANIMEAN_DEPLOY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ANIMEAN_PYTHON_HOME}"
                "${ANIMEAN_DEPLOY_DIR}/${ANIMEAN_PYTHON_DIR_NAME}"
            DEPENDS AnimeAn
            COMMENT "Deploying AnimeAn to ${ANIMEAN_DEPLOY_DIR}"
        )
    else()
        message(WARNING "windeployqt was not found; deploy_AnimeAn will not be available.")
    endif()
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

    pybind11_add_module(animean_python MODULE "${ANIMEAN_PYTHON_BINDINGS}" algorithm/animemodel.cpp algorithm/animemodel.h algorithm/vectorlogic.cpp algorithm/vectorlogic.h)
    target_include_directories(animean_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/algorithm ${CMAKE_CURRENT_SOURCE_DIR}/pythonbind)
    target_compile_definitions(animean_python PRIVATE ANIMEAN_BUILDING_PYTHON_MODULE)
    target_link_libraries(animean_python PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGLWidgets)
endif()

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.AnimeAn)
endif()
set_target_properties(AnimeAn PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS AnimeAn
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(AnimeAn)
endif()
