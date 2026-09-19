cmake_minimum_required(VERSION 3.20)
project(chess_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# A chess engine built without optimization is ~20x slower, which makes perft
# feel broken. Default to Release so nobody accidentally benchmarks a debug build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_NATIVE "Tune for the build machine's CPU (popcnt, bmi2, ...)" ON)

add_executable(chess
  src/main.cpp
  src/bitboard.cpp
  src/position.cpp
  src/movegen.cpp
  src/perft.cpp
  src/search.cpp
)

target_include_directories(chess PRIVATE src)

if(MSVC)
  target_compile_options(chess PRIVATE /W4 /permissive-)
  if(ENABLE_NATIVE)
    # MSVC has no -march=native; AVX2 implies POPCNT/BMI2, so std::popcount
    # and std::countr_zero become single instructions instead of fallbacks.
    target_compile_options(chess PRIVATE /arch:AVX2)
  endif()
else()
  target_compile_options(chess PRIVATE -Wall -Wextra -Wno-unused-parameter)
  if(ENABLE_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-march=native HAS_MARCH_NATIVE)
    if(HAS_MARCH_NATIVE)
      target_compile_options(chess PRIVATE -march=native)
    endif()
  endif()
endif()

# `ctest` runs the perft suite. Depth 4 keeps it to a couple of seconds;
# raise it (ctest runs `chess test <depth>`) once you trust the engine.
enable_testing()
add_test(NAME perft COMMAND chess test 4)
set_tests_properties(perft PROPERTIES TIMEOUT 300)
