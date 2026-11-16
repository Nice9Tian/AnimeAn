#!/usr/bin/env bash
# Prepares a fresh checkout for work on Linux: the pybind11 submodule, the web
# UI's packages, then a first build with the C++ tests. Safe to run again: the
# packages are installed afresh from the lock file, the build is incremental.
#
#   build_scripts/linux_setup.sh
#
# What has to be on the machine is listed in docs/mechanism/build_deploy.md.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

git -C "$repo_root" submodule update --init --depth 1

echo "== web UI packages (npm ci)"
(cd "$repo_root/webui" && npm ci --no-audit --no-fund --loglevel=error)

echo "== backend and C++ tests"
"$repo_root/build_scripts/linux_build.sh" --test

echo "===== LINUX SETUP DONE ====="
