cmake_minimum_required(VERSION 3.20)
project(spsc_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

option(SPSC_TSAN "Build with ThreadSanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

# ---- header-only library ----
add_library(spsc INTERFACE)
add_library(spsc::spsc ALIAS spsc)
target_include_directories(spsc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(spsc INTERFACE Threads::Threads)

# Common options for this project's own targets
add_library(spsc_warnings INTERFACE)
target_compile_options(spsc_warnings INTERFACE -Wall -Wextra -Wpedantic)
if(SPSC_TSAN)
    target_compile_options(spsc_warnings INTERFACE -fsanitize=thread)
    target_link_options(spsc_warnings INTERFACE -fsanitize=thread)
endif()

# ---- reference implementations in bench/ref (tests/bench only) ----
# conda's libboost-headers ships no CMake config, so just locate the headers.
find_path(SPSC_BOOST_INCLUDE_DIR boost/lockfree/spsc_queue.hpp REQUIRED)
add_library(spsc_compare INTERFACE)
target_include_directories(spsc_compare INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/bench)  # "ref/*.hpp"
target_include_directories(spsc_compare SYSTEM INTERFACE ${SPSC_BOOST_INCLUDE_DIR})  # silence Boost warnings
target_link_libraries(spsc_compare INTERFACE spsc)

# ---- tests (GoogleTest, from conda env "spsc") ----
enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test_queue tests/test_queue.cpp)
target_link_libraries(test_queue PRIVATE spsc spsc_compare spsc_warnings GTest::gtest_main)
if(SPSC_TSAN)
    set(SPSC_STRESS_N 200000)  # TSan is slow, use a smaller N
else()
    set(SPSC_STRESS_N 2000000)
endif()
gtest_discover_tests(test_queue PROPERTIES ENVIRONMENT "SPSC_STRESS_N=${SPSC_STRESS_N}")

# ---- bench (Google Benchmark, from conda env "spsc") ----
if(NOT SPSC_TSAN)
    find_package(benchmark REQUIRED)
    add_executable(bench bench/bench.cpp)
    target_link_libraries(bench PRIVATE spsc spsc_compare spsc_warnings benchmark::benchmark)
    target_compile_options(bench PRIVATE -O3 -march=native)
    target_compile_definitions(bench PRIVATE NDEBUG)
endif()
