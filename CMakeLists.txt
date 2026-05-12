cmake_minimum_required(VERSION 3.18)
project(SeePlusPlus LANGUAGES CXX CUDA)
# CUDA architecture(3090)
set(CMAKE_CUDA_ARCHITECTURES 86 90) 

set(CMAKE_CXX_STANDARD 17)

# ffmpeg path
set(FFMPEG_ROOT ${CMAKE_SOURCE_DIR}/third_party/ffmpeg)

include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

add_executable(vid_to_rgb src/test.cpp)

find_package(CUDAToolkit REQUIRED)
# lib link
target_link_libraries(vid_to_rgb
    avformat
    avcodec
    swscale
    avutil
)

if (CUDAToolkit_FOUND)
    message(STATUS "CUDA Found! Building with GPU support.")
    add_definitions(-DUSE_GPU)
    set(DECODER_SOURCES src/VideoDecoder.cpp src/GpuVideoDecoder.cu)
else()
    message(STATUS "CUDA not found. Building CPU version only.")
    set(DECODER_SOURCES src/VideoDecoder.cpp src/CpuVideoDecoder.cpp)
endif()

add_executable(SeePlusPlus src/test.cpp ${DECODER_SOURCES})


set_target_properties(vid_to_rgb PROPERTIES INSTALL_RPATH "${FFMPEG_ROOT}/lib")
target_link_libraries(SeePlusPlus PRIVATE CUDA::cudart avformat avcodec swscale avutil)
