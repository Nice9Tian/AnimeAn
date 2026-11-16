#!/usr/bin/env bash
# Builds the Windows programs on Linux with MinGW-w64, to check that a change
# made here still compiles and links for Windows, the system AnimeAn ships on.
# Nothing built here is run or released: the release comes from
# build_scripts/agent_build.ps1 on a Windows machine.
#
#   build_scripts/windows_cross_check.sh            incremental
#   build_scripts/windows_cross_check.sh --clean    start from an empty build/windows-cross
#
# Needs x86_64-w64-mingw32-g++-posix on PATH, and ANIMEAN_WINDOWS_PYTHON_HOME
# naming a Windows Python 3.12: the folder with include/Python.h and
# libs/python312.lib (what python.org's installer or its NuGet package lays
# down). No Python of this machine is involved. ccache is used when it is
# installed.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/windows-cross"

while [ $# -gt 0 ]; do
  case "$1" in
    --clean) rm -rf "$build_dir"; shift ;;
    -h|--help) sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

windows_python="${ANIMEAN_WINDOWS_PYTHON_HOME:-}"
if [ ! -f "$windows_python/include/Python.h" ]; then
  echo "ANIMEAN_WINDOWS_PYTHON_HOME must name a Windows Python 3.12 with include/ and libs/; it is '$windows_python'." >&2
  exit 1
fi
windows_python=$(realpath -s "$windows_python")
if ! command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1; then
  echo "x86_64-w64-mingw32-g++-posix is not installed (Debian/Ubuntu: g++-mingw-w64-x86-64-posix)." >&2
  exit 1
fi

# A fresh checkout or worktree has an empty external/pybind11.
if [ ! -f "$repo_root/external/pybind11/CMakeLists.txt" ]; then
  git -C "$repo_root" submodule update --init --depth 1
fi

configure_args=(
  -S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE="$repo_root/build_scripts/mingw_cross.cmake"
  -DANIMEAN_PYTHON_HOME="$windows_python"
)
if command -v ccache >/dev/null 2>&1; then
  configure_args+=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake "${configure_args[@]}"
cmake --build "$build_dir"

echo "===== WINDOWS CROSS CHECK DONE: $build_dir ====="
