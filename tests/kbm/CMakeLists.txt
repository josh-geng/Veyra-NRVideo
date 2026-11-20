# Standalone build of the keyboard/mouse engine and its tests (any OS, no Qt/Win32/Chiaki):
#   cmake -S tests/kbm -B build-kbm -G Ninja && cmake --build build-kbm && ctest --test-dir build-kbm
# The Windows product build includes the same module through cmake/VeyraRemotePlay.cmake.
cmake_minimum_required(VERSION 3.24)
project(veyra_kbm_tests CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/VeyraKbm.cmake")

# Windows CI: also compile (not link) the Win32 raw-input shim against real Qt and
# Windows SDK headers, so it is checked by MSVC without the full product dependencies.
option(VEYRA_KBM_CHECK_WIN32_SHIM "Compile src/ui/Ps5KbmCapture.cpp as well (needs Qt 6)" OFF)
if(WIN32 AND VEYRA_KBM_CHECK_WIN32_SHIM)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui)
  set(VEYRA_KBM_REPO "${CMAKE_CURRENT_LIST_DIR}/../..")
  add_library(veyra_kbm_capture_check OBJECT
      "${VEYRA_KBM_REPO}/src/ui/Ps5KbmCapture.cpp"
      "${VEYRA_KBM_REPO}/include/veyra/ui/Ps5KbmCapture.h")
  set_target_properties(veyra_kbm_capture_check PROPERTIES AUTOMOC ON)
  target_compile_definitions(veyra_kbm_capture_check PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(veyra_kbm_capture_check PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
  target_link_libraries(veyra_kbm_capture_check PRIVATE veyra_remoteplay_kbm Qt6::Core Qt6::Gui)
endif()
