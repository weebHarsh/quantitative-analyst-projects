cmake_minimum_required(VERSION 3.20)
project(md LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MD_WERROR "Treat warnings as errors" ON)
option(MD_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# Flags shared by every target (propagated through mdlib).
add_library(md_options INTERFACE)
target_compile_options(md_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(MD_WERROR)
  target_compile_options(md_options INTERFACE -Werror)
endif()
if(MD_SANITIZE)
  target_compile_options(md_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(md_options INTERFACE -fsanitize=address,undefined)
endif()

# Compile-time build description reported by the benchmark (compiler id/version and flags).
string(TOUPPER "${CMAKE_BUILD_TYPE}" MD_BT_UPPER)
set(MD_BUILD_STRING "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}; -std=c++17 ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${MD_BT_UPPER}}; build_type=${CMAKE_BUILD_TYPE}; sanitize=${MD_SANITIZE}")
string(STRIP "${MD_BUILD_STRING}" MD_BUILD_STRING)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/md_build_info.hpp
     CONTENT "#pragma once\n#define MD_BUILD_STRING \"@MD_BUILD_STRING@\"\n" @ONLY)

add_library(mdlib STATIC
  src/fixed.cpp
  src/io.cpp
  src/parser_a.cpp
  src/parser_b.cpp
  src/book.cpp
  src/session.cpp
  src/stats.cpp
  src/replay.cpp
  src/merge.cpp
  src/export.cpp
  src/bench.cpp)
target_include_directories(mdlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(mdlib PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(mdlib PUBLIC md_options)

add_executable(mdtool src/mdtool.cpp)
target_link_libraries(mdtool PRIVATE mdlib)

add_executable(md_bench bench/md_bench.cpp)
target_link_libraries(md_bench PRIVATE mdlib)

add_executable(md_tests
  tests/test_main.cpp
  tests/test_fixed.cpp
  tests/test_parsers.cpp
  tests/test_book.cpp
  tests/test_session.cpp
  tests/test_stats.cpp
  tests/test_merge.cpp
  tests/test_export.cpp
  tests/test_golden.cpp
  tests/test_review_regressions.cpp)
target_compile_definitions(md_tests PRIVATE MD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(md_tests PRIVATE mdlib)

enable_testing()
add_test(NAME md_tests COMMAND md_tests)
