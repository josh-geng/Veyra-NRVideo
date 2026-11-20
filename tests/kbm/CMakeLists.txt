# Standalone build of the keyboard/mouse engine and its tests (any OS, no Qt/Win32/Chiaki):
#   cmake -S tests/kbm -B build-kbm -G Ninja && cmake --build build-kbm && ctest --test-dir build-kbm
# The Windows product build includes the same module through cmake/VeyraRemotePlay.cmake.
cmake_minimum_required(VERSION 3.24)
project(veyra_kbm_tests CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/VeyraKbm.cmake")
