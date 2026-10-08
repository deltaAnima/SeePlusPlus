# SeePlusPlus
👁️++

SeePlusPlus is a C++ tool that finds the **most representative frame** of a video.
It computes the pixel-wise average of every frame, then uses a nearest-neighbour search (L1 distance) to pick the real frame closest to that average.

The project is built for speed. Frames are stored in one 64-byte-aligned buffer, and the hot loops use OpenMP, SIMD (AVX2) and cache-friendly blocking.

> [!IMPORTANT]
> **This project is archived.** The repository is read-only and is kept for reference. See [Status](#status) for what was finished when it was archived.

---

## How it works

```
video file ──► CPU_video_decoder ──► VideoData (all frames, aligned)
                                        │
                                        ▼
                               find_average_frame ──► FrameData (average frame)
                                        │
                                        ▼
                                 KNN (L1 distance) ──► index of the closest real frame
```

1. **Decode**: [`CPU_video_decoder`](src/decoder.cpp) reads the video with OpenCV in chunks of 64 frames. It copies the frames into a single buffer in parallel, one thread per core.
2. **Average**: [`find_average_frame`](src/FindAvgFrame.cpp) sums every pixel across all frames into `uint32_t` accumulators, then divides by the frame count. The work is split into 4 KB blocks per OpenMP thread so the partial sums stay in cache.
3. **Match**: [`KNN.cpp`](src/KNN.cpp) compares each frame with the average frame using an AVX2 sum of absolute differences (`_mm256_sad_epu8`) and returns the index with the smallest error.
4. **Orchestrate**: [`SeePlusPlus::frameHunt`](src/seePlusPlus.cpp) runs these three steps on a file path.

### Memory layout

All frames live in one block allocated with `std::aligned_alloc(64, ...)`. Each frame is padded up to a multiple of 64 bytes, so every frame starts on a cache-line boundary and works with aligned SIMD loads.

| Field                | Meaning                                          |
|----------------------|--------------------------------------------------|
| `frame_size`         | `width * height * channels`: the real pixel bytes |
| `aligned_frame_size` | `frame_size` rounded up to a multiple of 64      |
| Frame `i` address    | `data + i * aligned_frame_size`                  |

Use the helpers in [`SPP_UTILS.hpp`](include/SPP_UTILS.hpp) to access frames instead of computing offsets by hand:

```cpp
#include "decoder.hpp"
#include "SPP_UTILS.hpp"

SPP_STRUCTS::VideoData* vd = CPU_video_decoder("clip.mp4");

uint8_t* first  = SPP_UTILS::get_frame(vd, 0);            // by index
uint8_t* at_2s  = SPP_UTILS::get_frame_at_time(vd, 2.0);  // by playtime (seconds)

SPP_UTILS::free_video_data(vd);
```

> [!WARNING]
> `VideoData::data` and `FrameData::data` come from `std::aligned_alloc`. Free them with `std::free` (or `SPP_UTILS::free_video_data`) and **never** with `delete`.

---

## Project structure

```
include/            Public headers
  SPP_STRUCTS.hpp     VideoData / FrameData structs
  SPP_UTILS.hpp       Frame access + free helpers
  decoder.hpp         CPU_video_decoder
  FindAvgFrame.hpp    find_average_frame
  KNN.hpp             Closest-frame search
  seePlusPlus.hpp     SeePlusPlus pipeline class
src/                Implementations + main.cpp
tests/              GoogleTest unit tests and Google Benchmark benchmarks
third_party/        Git submodules (cpp-logger, highway, googletest, benchmark)
old/                Earlier prototypes (not built)
UpdateLog/          Update notes
meeting summary/    Meeting notes
```

---

## Requirements

- Linux (CI and development use **Ubuntu 24.04**)
- A compiler with **C++23** support (GCC 13+ recommended)
- A CPU with **AVX2** (the build passes `-mavx2`)
- CMake 3.10+
- OpenCV 4.x (`libopencv-dev`)
- OpenMP (`libomp-dev`, or the OpenMP support that comes with GCC)

On Ubuntu:

```bash
sudo apt-get install -y build-essential cmake git libopencv-dev libomp-dev
```

---

## Building

> [!IMPORTANT]
> Initialise submodules before building. The dependencies are not downloaded automatically.
> ```bash
> git submodule update --init --recursive
> ```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Release is the default build type. To skip building the tests and benchmarks, add `-DBUILD_TESTS=OFF`.

### Running

```bash
./build/SeePlusPlus [-v | --verbose] [-h | --help]
```

`-v` turns on debug-level logging.

---

## Tests and benchmarks

The unit tests use synthetic videos, so they need no input files:

```bash
./build/test_find_avg_frame
# or
ctest --test-dir build
```

The benchmark times `find_average_frame` on a real video or on random synthetic frames, and can save the average frame as an image:

```bash
# Real video
./build/bench_find_avg_frame --video path/to/clip.mp4 --output avg.png

# Synthetic: WIDTH HEIGHT FRAME_COUNT
./build/bench_find_avg_frame --synthetic 1920 1080 60 --output avg.png

# Any Google Benchmark flag also works
./build/bench_find_avg_frame --video clip.mp4 \
    --benchmark_repetitions=10 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=results/bench.json --benchmark_out_format=json
```

> [!NOTE]
> `*.mp4` files are git-ignored. CI expects test clips at `tests/video/1k30frames.mp4` and `tests/video/4k60frames.mp4`.

### CI

[`.gitlab-ci.yml`](.gitlab-ci.yml) runs two stages on `ubuntu:24.04`:

- **build**: configures and builds in Release mode.
- **test**: runs the unit tests (as a JUnit report shown in the MR "Tests" tab) and the benchmarks on the 1K and 4K clips. The text, JSON and PNG results are kept as job artifacts.

---

## Status

The project is **archived**. This table shows the state of each part when it was archived.

| Component                           | State at archive time                          |
|-------------------------------------|------------------------------------------------|
| CPU video decoder                   | ✅ Done (file input)                            |
| Average frame (OpenMP + SIMD)       | ✅ Done, tested, benchmarked                    |
| KNN closest-frame search (AVX2)     | ⏸️ Partly implemented                           |
| `SeePlusPlus::frameHunt` pipeline   | ⏸️ Partly wired up; returns `nullptr`           |
| Live stream input (`isStream`)      | 💡 Idea, not started                            |
| Highway / CUDA acceleration         | 💡 Idea, not started                            |
| Python bindings (pybind11)          | 💡 Idea, not started                            |

---

## Contributing

This repository is archived and does not accept new changes. You are welcome to fork it.
The workflow the team used during development is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Code style is defined in [`.clang-format`](.clang-format).

---

## License

[MIT](LICENSE) © 2026 Justin_Choi
