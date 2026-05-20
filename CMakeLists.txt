cmake_minimum_required(VERSION 3.10)
project(SeePlusPlus)

set(cmake_cxx_standard 23)
set(cmake_cxx_standard_required on)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenCV REQUIRED)
find_package(OpenMP REQUIRED)

set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/highway) 

add_executable(SeePlusPlus
    src/main.cpp
    src/seePlusPlus.cpp
    src/decoder.cpp
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

target_link_libraries(SeePlusPlus PRIVATE ${OpenCV_LIBS} OpenMP::OpenMP_CXX hwy)

target_compile_options(SeePlusPlus PRIVATE -O3 -march=native)

message(STATUS "OpenCV library status:")
message(STATUS "    version: ${OpenCV_VERSION}")
message(STATUS "    libraries: ${OpenCV_LIBS}")
message(STATUS "    include path: ${OpenCV_INCLUDE_DIRS}")
