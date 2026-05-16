# Initial Update: SPP Video Decoder

SPP video decoder is a high-performance(kinda) C++ based video data extracter that considered future HPC updates such as SIMD and CUDA and live streaming.

## Key Features

- **SIMD Optimized Memory**: allocating every frame into 64 bytes alignment for AVX2/AVX-512 based optimization.
- **Multithreaded Decoding**: Maximized the decoding speed using OpenCV and `std::thread`
- **Flexible Data Access**: It supports and provides index-based access, playtime-based access, and raw pointer based access utilities.

## Prerequisites
- **C++ Standard**: C++17 or above (it is using`std::aligned_alloc`)
- **Library**: OpenCV 4.x or above
- **OS**: Ubuntu 24.04 docker container

## Memory Layout

To maximize the performance, it aligns each frame's start address with adding a small padding between each frame

- **Frame Start Address**: `base_ptr + (index * aligned_frame_size)`
- **Actual Frame Data**: `width * height * channels`
