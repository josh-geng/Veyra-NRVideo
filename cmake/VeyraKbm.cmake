# Keyboard/mouse → virtual DualSense engine for PS5 Remote Play.
# Portable C++20 (no Win32, no Qt): the same library and tests build on Windows and Linux.
include_guard(GLOBAL)
find_package(Threads REQUIRED)
set(VEYRA_KBM_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(veyra_remoteplay_kbm STATIC
    "${VEYRA_KBM_ROOT}/src/remoteplay/kbm/KbmTypes.cpp"
    "${VEYRA_KBM_ROOT}/src/remoteplay/kbm/MouseStick.cpp"
    "${VEYRA_KBM_ROOT}/src/remoteplay/kbm/KbmProfile.cpp"
    "${VEYRA_KBM_ROOT}/src/remoteplay/kbm/KbmEngine.cpp"
    "${VEYRA_KBM_ROOT}/src/remoteplay/kbm/Calibration.cpp")
target_include_directories(veyra_remoteplay_kbm PUBLIC "${VEYRA_KBM_ROOT}/include")
target_compile_features(veyra_remoteplay_kbm PUBLIC cxx_std_20)
set_target_properties(veyra_remoteplay_kbm PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(veyra_remoteplay_kbm PUBLIC Threads::Threads)
if(MSVC)
  # Same policy as VeyraWarnings.cmake. Warning-clean under MSVC 14.52 /W4 (2026-11-21).
  target_compile_options(veyra_remoteplay_kbm PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /WX)
else()
  target_compile_options(veyra_remoteplay_kbm PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

option(VEYRA_BUILD_KBM_TESTS "Build the keyboard/mouse engine unit tests" ON)
if(VEYRA_BUILD_KBM_TESTS)
  add_executable(veyra_kbm_tests "${VEYRA_KBM_ROOT}/tests/kbm/KbmTests.cpp")
  target_link_libraries(veyra_kbm_tests PRIVATE veyra_remoteplay_kbm)
  enable_testing()
  add_test(NAME remoteplay-kbm COMMAND veyra_kbm_tests)
  set_tests_properties(remoteplay-kbm PROPERTIES TIMEOUT 120)
endif()
