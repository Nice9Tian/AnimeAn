# CMake toolchain file: build the Windows programs on Linux with MinGW-w64, the
# compiler family the Windows release is built with. It is how a change made on
# Linux is checked against Windows without a Windows machine; see
# build_scripts/windows_cross_check.sh, which is what passes this file to CMake.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# The posix-threads flavour, like the MinGW the release uses (std::thread, std::mutex).
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Headers and libraries come from the Windows side only; programs (the host
# Python that answers pybind11's questions) from this machine.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
