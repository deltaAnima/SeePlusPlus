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
  src/FindAvgFrame.cpp
  src/decoder.cpp
)

target_link_libraries(SeePlusPlus PRIVATE
  project_common
  ${OpenCV_LIBS}
)

target_compile_options(SeePlusPlus PRIVATE -O3)

# --- Diagnostics ---
message(STATUS "OpenCV library status:")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")

message(STATUS "OpenMP library status:")
message(STATUS "    version: ${OpenMP_CXX_VERSION}")
message(STATUS "    libraries: ${OpenMP_CXX_LIBRARIES}")
message(STATUS "    include path: ${OpenMP_CXX_INCLUDE_DIRS}")

