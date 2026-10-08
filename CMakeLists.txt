cmake_minimum_required(VERSION 3.10)
project(SeePlusPlus LANGUAGES CXX)

# --- Global settings ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenCV REQUIRED)
find_package(OpenMP REQUIRED)

# --- Shared project settings ---
add_library(project_common INTERFACE)
target_include_directories(project_common INTERFACE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/third_party/cpp-logger
)
target_compile_features(project_common INTERFACE cxx_std_23)
target_link_libraries(project_common INTERFACE OpenMP::OpenMP_CXX)

target_compile_options(project_common INTERFACE -mavx2)

# --- Main executable ---
add_executable(SeePlusPlus
    src/main.cpp
    src/seePlusPlus.cpp
    src/decoder.cpp
    src/KNN.cpp
    src/FindAvgFrame.cpp
  )

if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP Include Dirs: ${OpenMP_CXX_INCLUDE_DIRS}")
  target_include_directories(SeePlusPlus PRIVATE ${OpenMP_CXX_INCLUDE_DIRS})
  target_include_directories(SeePlusPlus PRIVATE /usr/lib/gcc/x86_64-linux-gnu/11/include)
endif()

target_include_directories(SeePlusPlus PRIVATE 
  include
  third_party/cpp-logger
)

target_link_libraries(SeePlusPlus PRIVATE
  project_common
  ${OpenCV_LIBS}
)

target_compile_options(SeePlusPlus PRIVATE -O3)

# --- Unit tests ---
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
  enable_testing()

  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/googletest)
  add_subdirectory(third_party/benchmark)

  add_executable(test_find_avg_frame
    tests/test_find_avg_frame.cpp
    src/FindAvgFrame.cpp
  )
  target_link_libraries(test_find_avg_frame PRIVATE
    project_common
    GTest::gtest_main
  )

  add_executable(test_knn
    tests/test_knn.cpp
    src/KNN.cpp
  )
  target_link_libraries(test_knn PRIVATE
    project_common
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(test_find_avg_frame)
  gtest_discover_tests(test_knn)

  add_executable(bench_knn
    tests/bench_knn.cpp
    src/KNN.cpp
  )
  target_link_libraries(bench_knn PRIVATE
    project_common
    benchmark::benchmark_main
  )
  target_compile_options(bench_knn PRIVATE -O3)

  add_executable(bench_find_avg_frame
    tests/bench_find_avg_frame.cpp
    src/decoder.cpp
    src/FindAvgFrame.cpp
  )
  target_link_libraries(bench_find_avg_frame PRIVATE
    project_common
    ${OpenCV_LIBS}
    benchmark::benchmark_main
  )
  target_compile_options(bench_find_avg_frame PRIVATE -O3)
endif()

# --- Diagnostics ---
message(STATUS "OpenCV library status:")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")

message(STATUS "OpenMP library status:")
message(STATUS "    version: ${OpenMP_CXX_VERSION}")
message(STATUS "    libraries: ${OpenMP_CXX_LIBRARIES}")
message(STATUS "    include path: ${OpenMP_CXX_INCLUDE_DIRS}")
