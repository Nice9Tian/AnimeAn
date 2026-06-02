cmake_minimum_required(VERSION 3.16)

project(AnimeAn VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ANIMEAN_WITH_PYTHON "Enable pybind11 support for embedding Python in AnimeAn." ON)
option(ANIMEAN_BUILD_PYTHON_MODULE "Build the optional AnimeAn Python extension module." OFF)

if(ANIMEAN_WITH_PYTHON OR ANIMEAN_BUILD_PYTHON_MODULE)
    set(PYBIND11_FINDPYTHON ON)
    set(PYBIND11_TEST OFF CACHE BOOL "Build pybind11 tests" FORCE)
    add_subdirectory(external/pybind11)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets OpenGLWidgets)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        paintopenglwidget.h
        vectorinput.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(AnimeAn
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET AnimeAn APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(AnimeAn SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(AnimeAn
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(AnimeAn PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGLWidgets)

if(ANIMEAN_WITH_PYTHON)
    target_link_libraries(AnimeAn PRIVATE pybind11::embed)
    target_compile_definitions(AnimeAn PRIVATE ANIMEAN_WITH_PYTHON)
endif()

target_include_directories(AnimeAn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(ANIMEAN_BUILD_PYTHON_MODULE)
    set(ANIMEAN_PYTHON_BINDINGS "${CMAKE_CURRENT_SOURCE_DIR}/python_bindings.cpp" CACHE FILEPATH "Source file for the AnimeAn Python extension module.")

    if(NOT EXISTS "${ANIMEAN_PYTHON_BINDINGS}")
        message(FATAL_ERROR "ANIMEAN_BUILD_PYTHON_MODULE is ON, but ANIMEAN_PYTHON_BINDINGS does not exist: ${ANIMEAN_PYTHON_BINDINGS}")
    endif()

    pybind11_add_module(animean_python MODULE "${ANIMEAN_PYTHON_BINDINGS}")
    target_include_directories(animean_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(animean_python PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::OpenGLWidgets)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
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
