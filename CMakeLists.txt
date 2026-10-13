cmake_minimum_required(VERSION 3.20)
project(signal_pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

add_library(sp_core
  src/fft.cpp
  src/packet.cpp
  src/receiver.cpp
  src/worker.cpp
  src/bench.cpp
)
target_include_directories(sp_core PUBLIC src)
target_link_libraries(sp_core PUBLIC Threads::Threads)
target_compile_options(sp_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(signal_pipeline src/main.cpp)
target_link_libraries(signal_pipeline PRIVATE sp_core)

option(SP_BUILD_TESTS "Build unit tests" ON)
if(SP_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_executable(sp_tests tests/test_fft.cpp tests/test_queue.cpp tests/test_receiver.cpp tests/test_pipeline.cpp)
  target_link_libraries(sp_tests PRIVATE sp_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(sp_tests)
endif()
