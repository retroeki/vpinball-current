# license:GPLv3+
#
# Standalone build for patch_corpus -- runs the real SimpleScriptPatcher over a
# whole corpus of extracted VBScripts. Mirrors the abseil + RE2 wiring of the
# main build (root CMakeLists.txt:43-51) using the vendored sources, so it is
# byte-faithful to the on-device regex engine.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/patch_corpus <in_dir> <out_dir> [report.csv]

cmake_minimum_required(VERSION 3.16)
project(patch_corpus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE Release)
endif()

# Repo root: tests/scriptpatcher/corpus_harness -> ../../..
get_filename_component(VPX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# The src tree holding ui/simplescriptpatcher.cpp. patcher_diff.sh points it at a copy of
# another git revision to build a baseline patcher.
set(PATCHER_SRC "${VPX_ROOT}/src" CACHE PATH "src tree of the patcher under test")

find_package(Threads REQUIRED)

# Vendored Abseil (required by RE2) + RE2, EXCLUDE_FROM_ALL so only the targets
# patch_corpus actually needs get built.
set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_BUILD_TESTING OFF)
add_subdirectory("${VPX_ROOT}/abseil-cpp" "${CMAKE_BINARY_DIR}/abseil-build" EXCLUDE_FROM_ALL)
set(RE2_BUILD_TESTING OFF)
set(RE2_INSTALL OFF)
add_subdirectory("${VPX_ROOT}/re2" "${CMAKE_BINARY_DIR}/re2-build" EXCLUDE_FROM_ALL)

add_executable(patch_corpus
   patch_corpus.cpp
   "${PATCHER_SRC}/ui/simplescriptpatcher.cpp"   # the patcher under test
   "${PATCHER_SRC}/ui/tablepatches.cpp"          # per-table layer (std::regex, self-contained)
)

target_compile_definitions(patch_corpus PRIVATE __STANDALONE__)

# Order matters: this dir is FIRST so `#include "stdafx.h"` resolves to our
# minimal shim instead of the heavy src/core/stdafx.h.
target_include_directories(patch_corpus BEFORE PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}"          # stdafx.h shim
   "${PATCHER_SRC}"                      # ui/simplescriptpatcher.h, ui/tablepatches.h
   "${VPX_ROOT}/re2"                      # <re2/re2.h>
   "${VPX_ROOT}/third-party/include"      # <plog/Log.h>
)

target_link_libraries(patch_corpus PRIVATE re2 Threads::Threads)
