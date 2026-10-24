#!/usr/bin/env bash
# Build vbs_check (host build of the shipped Wine vbscript engine) and run the
# regression cases. Usage (WSL/Linux): ./run.sh [extra run_cases.py args]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${VBS_CHECK_BUILD:-$HOME/vbs_check_build}"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > "$BUILD.cmake.log" 2>&1 || { cat "$BUILD.cmake.log"; exit 1; }
cmake --build "$BUILD" -j"$(nproc)" > "$BUILD.build.log" 2>&1 || { grep -E 'error|undefined' "$BUILD.build.log" | head -20; exit 1; }
VBS_CHECK="$BUILD/vbs_check" python3 "$HERE/run_cases.py" "$@"
