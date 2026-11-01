option(ANIMEAN_BUILD_BACKEND "Build the headless AnimeAn backend." ON)

if(NOT ANIMEAN_BUILD_BACKEND)
    return()
endif()

add_subdirectory(platform)
add_subdirectory(net)

add_library(animean_core_headless STATIC
    ${CMAKE_SOURCE_DIR}/selectionattention.cpp
    ${CMAKE_SOURCE_DIR}/selectionattention.h
    ${CMAKE_SOURCE_DIR}/boardbase.h
    ${CMAKE_SOURCE_DIR}/openglwidget.cpp
    ${CMAKE_SOURCE_DIR}/openglwidget.h
    ${CMAKE_SOURCE_DIR}/theme.cpp
    ${CMAKE_SOURCE_DIR}/theme.h
    board/boardsurface.cpp
    board/boardsurface.h
    board/cursor.h
    board/inputevent.h
    board/recordingdevice.cpp
    board/recordingdevice.h
    board/textpath.cpp
    board/textpath.h
)

target_include_directories(animean_core_headless PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/algorithm
    ${CMAKE_SOURCE_DIR}/pythonbind
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/board
)

target_link_libraries(animean_core_headless PUBLIC animean_core animean_platform gdi32)

if(ANIMEAN_WITH_PYTHON)
    target_sources(animean_core_headless PRIVATE
        ${CMAKE_SOURCE_DIR}/pythonbind/python_bindings.cpp
        ${CMAKE_SOURCE_DIR}/pythonbind/python_bindings.h
    )
    target_link_libraries(animean_core_headless PUBLIC pybind11::embed)
    target_compile_definitions(animean_core_headless PUBLIC
        ANIMEAN_WITH_PYTHON
        ANIMEAN_SOURCE_DIR="${ANIMEAN_SOURCE_DIR}"
        ANIMEAN_PYFILE_DIR="${ANIMEAN_PYFILE_DIR}"
        ANIMEAN_PYTHONBIND_DIR="${ANIMEAN_PYTHONBIND_DIR}"
        ANIMEAN_OPENTOONZ_TOOLS_DIR="${ANIMEAN_OPENTOONZ_TOOLS_DIR}"
        ANIMEAN_PYTHON_HOME="${ANIMEAN_PYTHON_HOME}"
        ANIMEAN_PYTHON_DIR_NAME="${ANIMEAN_PYTHON_DIR_NAME}"
    )
endif()

add_executable(AnimeAnBackend
    main.cpp
    net/wsserver.cpp
    net/wsserver.h
    business/animeanbusiness.cpp
    business/animeanbusiness.h
    session/backendsession.cpp
    session/backendsession.h
)
target_link_libraries(AnimeAnBackend PRIVATE animean_core_headless animean_platform ws2_32)

add_executable(board_headless_test
    board/tests/board_headless_test.cpp
)
target_link_libraries(board_headless_test PRIVATE animean_core_headless)
add_dependencies(AnimeAnBackend board_headless_test)

add_executable(textpath_tests
    board/tests/textpath_tests.cpp
)
target_link_libraries(textpath_tests PRIVATE animean_core_headless)
add_dependencies(AnimeAnBackend textpath_tests)

if(ANIMEAN_WITH_PYTHON)
    foreach(ANIMEAN_PYTHON_DLL python312.dll python3.dll vcruntime140.dll vcruntime140_1.dll)
        if(EXISTS "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}")
            add_custom_command(TARGET AnimeAnBackend POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}"
                    "$<TARGET_FILE_DIR:AnimeAnBackend>"
            )
            add_custom_command(TARGET board_headless_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ANIMEAN_PYTHON_HOME}/${ANIMEAN_PYTHON_DLL}"
                    "$<TARGET_FILE_DIR:board_headless_test>"
            )
        endif()
    endforeach()
endif()

if(BUILD_TESTING)
    add_test(NAME board_headless_test
        COMMAND board_headless_test "${CMAKE_CURRENT_BINARY_DIR}/board_headless_ops.json"
    )
    add_test(NAME textpath_tests
        COMMAND textpath_tests
    )
endif()
