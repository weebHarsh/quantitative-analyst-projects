cmake_minimum_required(VERSION 3.20)
project(bt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BT_WERROR "Treat warnings as errors" ON)
option(BT_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# Flags shared by every bt target (same set as Project 3).
add_library(bt_options INTERFACE)
target_compile_options(bt_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(BT_WERROR)
  target_compile_options(bt_options INTERFACE -Werror)
endif()
if(BT_SANITIZE)
  target_compile_options(bt_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(bt_options INTERFACE -fsanitize=address,undefined)
endif()

# Project 3 (mdlib, mdtool) is built from its own CMakeLists, unmodified. Its options follow ours.
set(MD_WERROR ${BT_WERROR} CACHE BOOL "" FORCE)
set(MD_SANITIZE ${BT_SANITIZE} CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../03-market-data ${CMAKE_BINARY_DIR}/md EXCLUDE_FROM_ALL)

add_library(bt_lib STATIC
  src/types.cpp
  src/event_csv.cpp
  src/queue.cpp
  src/latency.cpp
  src/event_queue.cpp
  src/strategy_mm.cpp
  src/strategy_schedule.cpp
  src/backtest.cpp
  src/output.cpp)
target_include_directories(bt_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bt_lib PUBLIC mdlib bt_options)

add_executable(bt_run src/bt_run.cpp)
target_link_libraries(bt_run PRIVATE bt_lib)

add_executable(bt_tests
  tests/test_main.cpp
  tests/test_event_csv.cpp
  tests/test_queue.cpp
  tests/test_latency.cpp
  tests/test_exchange.cpp
  tests/test_strategy_mm.cpp
  tests/test_backtest.cpp
  tests/test_determinism.cpp)
target_compile_definitions(bt_tests PRIVATE
  BT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  BT_MD_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../03-market-data/tests/golden")
target_link_libraries(bt_tests PRIVATE bt_lib)

enable_testing()
add_test(NAME bt_tests COMMAND bt_tests)
