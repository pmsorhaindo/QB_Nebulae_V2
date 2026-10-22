cmake_minimum_required(VERSION 3.15)
project(LunarHazeAU LANGUAGES CXX)

# Builds the portable DSP core plus the offline tools on Linux/macOS/Windows.
# The iOS AUv3 itself is built from project.yml (XcodeGen) on a Mac.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(lunarhaze_dsp STATIC
  dsp/LunarHazeDSP.cpp
  dsp/LunarHazeParams.cpp)
target_include_directories(lunarhaze_dsp PUBLIC dsp)
target_compile_options(lunarhaze_dsp PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

add_executable(lunarhaze_render tools/render_offline.cpp)
target_link_libraries(lunarhaze_render PRIVATE lunarhaze_dsp)

add_executable(lunarhaze_tests tools/lunarhaze_tests.cpp)
target_link_libraries(lunarhaze_tests PRIVATE lunarhaze_dsp)

enable_testing()
add_test(NAME lunarhaze_core COMMAND lunarhaze_tests)
