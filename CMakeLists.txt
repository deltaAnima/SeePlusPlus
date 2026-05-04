cmake_minimum_required(VERSION 3.10)
project(VidToRGB)

set(CMAKE_CXX_STANDARD 17)

# ffmpeg path
set(FFMPEG_ROOT ${CMAKE_SOURCE_DIR}/third_party/ffmpeg)

include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

add_executable(vid_to_rgb src/test.cpp)

# lib link
target_link_libraries(vid_to_rgb
    avformat
    avcodec
    swscale
    avutil
)

set_target_properties(vid_to_rgb PROPERTIES INSTALL_RPATH "${FFMPEG_ROOT}/lib")
