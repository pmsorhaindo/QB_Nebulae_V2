cmake_minimum_required(VERSION 3.15)
project(CosmicFluxAU LANGUAGES CXX)

# Builds the portable DSP core plus the offline tools on Linux/macOS/Windows.
# The iOS AUv3 itself is built from project.yml (XcodeGen) on a Mac.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(cosmicflux_dsp STATIC
  dsp/CosmicFluxDSP.cpp
  dsp/CosmicFluxParams.cpp)
target_include_directories(cosmicflux_dsp PUBLIC dsp)
target_compile_options(cosmicflux_dsp PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

add_executable(cosmicflux_render tools/render_offline.cpp)
target_link_libraries(cosmicflux_render PRIVATE cosmicflux_dsp)

add_executable(cosmicflux_tests tools/cosmicflux_tests.cpp)
target_link_libraries(cosmicflux_tests PRIVATE cosmicflux_dsp)

enable_testing()
add_test(NAME cosmicflux_core COMMAND cosmicflux_tests)
