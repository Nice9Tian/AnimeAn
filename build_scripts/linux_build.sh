#!/usr/bin/env bash
# Development build on Linux: configures and builds the backend and the C++
# tests into build/linux. There is no Linux release package; what ships is the
# Windows folder that build_scripts/agent_build.ps1 makes.
#
#   build_scripts/linux_build.sh                 incremental build
#   build_scripts/linux_build.sh --test          build, then run the C++ tests (ctest)
#   build_scripts/linux_build.sh --clean         start from an empty build folder
#   build_scripts/linux_build.sh --target NAME   build one target (default: all)
#   build_scripts/linux_build.sh --dir DIR       another build folder (default: build/linux)
#
# The Python runtime to embed is read from ANIMEAN_PYTHON_HOME: a full Python
# 3.12 with headers and its shared library. Tools needed: cmake, ninja, a C++17
# compiler, the FreeType and Fontconfig development files. ccache is used when
# it is installed.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build/linux"
target=""
run_tests=0
clean=0

while [ $# -gt 0 ]; do
  case "$1" in
    --test) run_tests=1; shift ;;
    --clean) clean=1; shift ;;
    --target) target="${2:?--target needs a name}"; shift 2 ;;
    --dir) build_dir="${2:?--dir needs a folder}"; shift 2 ;;
    -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -d "${ANIMEAN_PYTHON_HOME:-}" ]; then
  echo "ANIMEAN_PYTHON_HOME must name a full Python 3.12 (headers and shared library); it is '${ANIMEAN_PYTHON_HOME:-}'." >&2
  exit 1
fi
# The folder is compiled into the programs, so it has to mean the same from
# anywhere: made absolute, links left as they are (a "3.12" link that later
# points at a newer patch version is followed without a rebuild). CMake keeps
# the first value it saw for a build folder: after pointing the variable
# somewhere else, build with --clean.
ANIMEAN_PYTHON_HOME=$(realpath -s "$ANIMEAN_PYTHON_HOME")
export ANIMEAN_PYTHON_HOME

# A fresh checkout or worktree has an empty external/pybind11.
if [ ! -f "$repo_root/external/pybind11/CMakeLists.txt" ]; then
  git -C "$repo_root" submodule update --init --depth 1
fi

if [ "$clean" = 1 ]; then
  rm -rf "$build_dir"
fi

configure_args=(-S "$repo_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release)
if command -v ccache >/dev/null 2>&1; then
  configure_args+=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake "${configure_args[@]}"

if [ -n "$target" ]; then
  cmake --build "$build_dir" --target "$target"
else
  cmake --build "$build_dir"
fi

if [ "$run_tests" = 1 ]; then
  ctest --test-dir "$build_dir" --output-on-failure
fi

echo "===== LINUX BUILD DONE: $build_dir ====="
