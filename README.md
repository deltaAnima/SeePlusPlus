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

1. **Decode**: [`CPU_video_decoder`](src/decoder.cpp) reads the video with OpenCV in chunks of 64 frames. It copies the frames into a single buffer in parallel, one thread per core, using an AVX2 non-temporal copy written in inline assembly.
2. **Average**: [`find_average_frame`](src/FindAvgFrame.cpp) sums every pixel across all frames into `uint32_t` accumulators, then divides by the frame count. The work is split into 4 KB blocks per OpenMP thread so the partial sums stay in cache.
3. **Match**: [`findSmallestErr`](src/KNN.cpp) compares each frame with the average frame using an AVX2 sum of absolute differences (`_mm256_sad_epu8`, 32 bytes per instruction). OpenMP splits the frames across threads. Each thread keeps its own minimum, and the minimums are merged once at the end. On a tie, the lowest frame index wins.
4. **Orchestrate**: [`SeePlusPlus::frameHunt`](src/seePlusPlus.cpp) runs these three steps on a file path.

### Memory layout

All frames live in one block allocated with `std::aligned_alloc` on a 2 MB boundary, and the decoder calls `madvise(MADV_HUGEPAGE)` on it so the kernel can back it with 2 MB pages instead of 4 KB pages (see [Decoder benchmark](#decoder-benchmark)). Each frame is padded up to a multiple of 64 bytes, so every frame starts on a cache-line boundary and works with aligned SIMD loads.

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
./build/test_knn
# or
ctest --test-dir build
```

### Average frame benchmark

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

### KNN tests and benchmark

[`test_knn`](tests/test_knn.cpp) checks that:
- the AVX2 L1 kernel gives exactly the same result as a plain scalar version, for sizes around the 32-byte SIMD step (0, 1, 31, 32, 33, …) up to a full 1080p frame
- a 4K frame where every byte differs by 255 does not overflow
- `findSmallestErr` finds a planted frame and agrees with a brute-force scalar search
- ties return the lowest index, and null or empty input returns `SIZE_MAX`

[`bench_knn`](tests/bench_knn.cpp) runs on synthetic random frames (30 frames per resolution, no video file needed). It compares the AVX2 kernel and the full `findSmallestErr` search against a scalar baseline:

```bash
./build/bench_knn --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

Results on an AMD Ryzen 7 7840U laptop (8 cores / 16 threads), GCC 16.2, `-O3 -mavx2`. Each number is the median of 5 runs:

| Search over 30 frames | Scalar, 1 thread | AVX2 + OpenMP | Speedup |
|-----------------------|-----------------:|--------------:|--------:|
| 720p                  | 13.5 ms          | 2.14 ms       | 6.3×    |
| 1080p                 | 31.8 ms          | 4.98 ms       | 6.4×    |
| 4K                    | 129 ms           | 24.7 ms       | 5.2×    |

| L1 distance, one frame (1 thread) | Scalar  | AVX2    | Speedup |
|-----------------------------------|--------:|--------:|--------:|
| 1080p                             | 1078 µs | 197 µs  | 5.5×    |
| 4K                                | 4890 µs | 1908 µs | 2.6×    |

The scalar baseline is ordinary C++ compiled with the same flags, so the compiler is free to auto-vectorize it. The speedup is measured against what the compiler already produces. At 4K, the data is larger than the 16 MB L3 cache, and the search is limited by memory bandwidth (about 28–35 GiB/s), not by computation.

> [!NOTE]
> The KNN tests and benchmark ([`tests/test_knn.cpp`](tests/test_knn.cpp), [`tests/bench_knn.cpp`](tests/bench_knn.cpp)) were written by Claude (an AI assistant), not by the team. The numbers above come from Claude's run(Benchmarks were run on my laptop (Ryzen 7 7840U)).

### Decoder benchmark

This benchmark compares four versions of `CPU_video_decoder`. The versions differ only in how each decoded frame is copied into the buffer and in the page size of that buffer:

- **memcpy, 4 KB pages**: the decoder before this change (`af61ea6`).
- **ASM + prefetch, 4 KB pages**: the AVX2 non-temporal copy (`vmovntdq`) with `prefetchnta`, on a buffer aligned to 64 bytes.
- **ASM + prefetch, 2 MB pages**: the current decoder. The buffer is aligned to 2 MB and marked with `madvise(MADV_HUGEPAGE)`.
- **memcpy, 2 MB pages**: for comparison.

Setup: AMD Ryzen 7 7840U laptop (8 cores / 16 threads), Ubuntu 24.04 (kernel 7.0), GCC 13.3, OpenCV 4.6 (FFmpeg backend), `-O3 -mavx2`. Transparent huge pages were set to `madvise`. The input clips are synthetic MPEG-4 Part 2 (`mp4v`) videos made with OpenCV: 1080p with 300 frames, and 4K with 120 frames. Each number is the median of 9 runs, and the order of the four versions was rotated on every run.

| Decoder                     | 1080p, 300 frames | 4K, 120 frames |
|-----------------------------|------------------:|---------------:|
| memcpy, 4 KB pages (before) | 627 ms            | 1656 ms        |
| ASM + prefetch, 4 KB pages  | 695 ms            | 1642 ms        |
| **ASM + prefetch, 2 MB pages (current)** | **614 ms** | **1476 ms** |
| memcpy, 2 MB pages          | 666 ms            | 1522 ms        |

What the numbers show:

- **Page faults cost more than the copy itself.** The buffer is about 2 GB, so with 4 KB pages the first write to it causes about 500,000 page faults. With 2 MB pages, there are 512 times fewer. Switching to 2 MB pages made the ASM decoder 12% faster at 1080p and 10% faster at 4K.
- **The ASM copy only helps on 2 MB pages.** On 4 KB pages, the ASM copy was about 10% slower than `memcpy` at 1080p. On 2 MB pages, it was faster than `memcpy` at both resolutions.
- **`prefetchnta` made no measurable difference.** The loop reads the source in order, and the hardware prefetcher already handles that pattern.
- **Decoding is now the bottleneck.** `cap.read()` alone takes about 300 ms for the 1080p clip and runs on a single thread. Further speedups need parallel decoding.

#### Breakdown with `bench_decoder`

[`bench_decoder`](tests/bench_decoder.cpp) splits the decoder's time into its parts. It does not contain the old decoder versions. Instead, it times the real `CPU_video_decoder`, decoding alone, and the copy step with each copy function and page size:

| Benchmark                        | What it measures                                                    |
|----------------------------------|---------------------------------------------------------------------|
| `decoder/full`                   | the real `CPU_video_decoder`                                        |
| `decoder/decode_only`            | `cv::VideoCapture::read()` alone, with no copy                      |
| `copy/<memcpy\|asm>/<4KB\|2MB>`  | copying every frame into a new buffer, including page faults        |
| `copy/<memcpy\|asm>/prefaulted`  | copying into a buffer that was already written once (no page faults) |

The copy benchmarks copy in 64-frame chunks across all hardware threads, the same way the decoder does.

```bash
# Real video
./build/bench_decoder --video path/to/clip.mp4

# Synthetic: WIDTH HEIGHT FRAME_COUNT (writes an mp4v clip to the temp directory first)
./build/bench_decoder --synthetic 1920 1080 300 \
    --benchmark_repetitions=9 --benchmark_report_aggregates_only=true
```

Results on the same laptop, median of 9 runs:

| Benchmark                | 1080p, 300 frames | 4K, 120 frames |
|--------------------------|------------------:|---------------:|
| `decoder/full`           | 595 ms            | 1442 ms        |
| `decoder/decode_only`    | 301 ms            | 796 ms         |
| `copy/memcpy/4KB`        | 152 ms            | 382 ms         |
| `copy/asm/4KB`           | 224 ms            | 375 ms         |
| `copy/memcpy/2MB`        | 180 ms            | 235 ms         |
| `copy/asm/2MB`           | 126 ms            | 203 ms         |
| `copy/memcpy/prefaulted` | 118 ms            | 125 ms         |
| `copy/asm/prefaulted`    | 74 ms             | 117 ms         |

Decoding takes about half of the total time. Comparing the `prefaulted` rows with the `4KB` rows shows the cost of page faults. The ASM copy is the fastest option once page faults are reduced, which is the `2MB` and `prefaulted` rows.

> [!NOTE]
> - The huge page improvement needs transparent huge pages set to `always` or `madvise` (`cat /sys/kernel/mm/transparent_hugepage/enabled`). With `never`, the decoder falls back to 4 KB pages.
> - The four-version table above was measured with copies of each decoder version made for a one-off comparison. Those copies are not in the repository. `bench_decoder` reproduces the parts that explain the results. Results varied by a few percent between runs, and real H.264/H.265 clips will have different decode costs.
> - Every frame from the current decoder was checked byte for byte against frames read directly with `cv::VideoCapture`, for both clips.
> - This benchmark was run and written up by Claude (an AI assistant) on the Ryzen 7 7840U laptop.

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
| KNN closest-frame search (AVX2)     | ✅ Done, tested, benchmarked (not yet called by `frameHunt`) |
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
