//   REAL:      ./bench_decoder --video path/to/video.mp4 [google benchmark flags...]
//   SYNTHETIC: ./bench_decoder --synthetic WIDTH HEIGHT FRAME_COUNT [google benchmark flags...]
//
// Measures where CPU_video_decoder spends its time:
//
//   decoder/full                  the real CPU_video_decoder (decode + copy + page faults)
//   decoder/decode_only           cv::VideoCapture::read() alone, no copy
//   copy/<memcpy|asm>/<4KB|2MB>   copy into a FRESH buffer, so page faults are included
//   copy/<memcpy|asm>/prefaulted  copy into a buffer that was already written once
//
// The copy benchmarks use the same pattern as the decoder: 64-frame chunks,
// split across std::thread::hardware_concurrency() threads, every frame at a
// 64-byte aligned offset. The source is the first 64 decoded frames, reused
// for every chunk, so only the destination buffer grows with the frame count.
//
// --synthetic writes an MPEG-4 Part 2 (mp4v) clip with OpenCV to the temp
// directory first, so it still goes through the real decoder.
//
// Example:
//   ./bench_decoder --synthetic 1920 1080 300 \
//       --benchmark_repetitions=9 \
//       --benchmark_report_aggregates_only=true

#include "SPP_STRUCTS.hpp"
#include "SPP_UTILS.hpp"

#include "decoder.hpp"

#include <benchmark/benchmark.h>

#include <sys/mman.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

static constexpr size_t CHUNK     = 64;
static constexpr size_t ALIGN     = 64;
static constexpr size_t HUGE_PAGE = 2 * 1024 * 1024;

static std::string read_first_line(const char* path) {
  std::ifstream f(path);
  std::string line;
  std::getline(f, line);
  return line;
}

static std::string cpu_model_name() {
  std::ifstream f("/proc/cpuinfo");
  std::string line;

  while (std::getline(f, line)) {
    if (line.rfind("model name", 0) == 0) {
      auto pos = line.find(':');
      if (pos != std::string::npos) {
        return line.substr(pos + 2);
      }
    }
  }
  return "unknown";
}

// ── synthetic clip (gradient + a little noise, so it is not trivial to encode) ──

static bool write_synthetic_clip(const std::string& path, int width, int height, int count) {
  std::printf("→ writing synthetic clip %s (%dx%d, %d frames) ...\n",
      path.c_str(), width, height, count);

  cv::VideoWriter vw(path, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30, {width, height});
  if (!vw.isOpened()) {
    std::fprintf(stderr, "error: cv::VideoWriter could not open '%s'\n", path.c_str());
    return false;
  }

  cv::Mat frame(height, width, CV_8UC3);
  std::mt19937 rng(42);
  for (int i = 0; i < count; ++i) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        frame.at<cv::Vec3b>(y, x) = {
          uint8_t(x + i), uint8_t(y + 2 * i), uint8_t((x ^ y) + rng() % 16)};
      }
    }
    vw.write(frame);
  }
  return true;
}

// ── global fixture state ────────────────────────────────────────────────

static std::string          g_video_path;
static size_t               g_frame_count = 0;     // real number of frames in the clip
static size_t               g_frame_size  = 0;
static size_t               g_aligned_frame_size = 0;
static std::vector<cv::Mat> g_source;               // first CHUNK decoded frames

// ── copy helpers ────────────────────────────────────────────────────────

using CopyFn = void (*)(uint8_t*, const uint8_t*, size_t);

static void copy_memcpy(uint8_t* dest, const uint8_t* src, size_t bytes) {
  std::memcpy(dest, src, bytes);
}

static void copy_asm(uint8_t* dest, const uint8_t* src, size_t bytes) {
  ASM_streaming_copy_avx2(dest, src, bytes);
}

// Same loop shape as CPU_video_decoder: one 64-frame chunk at a time, split
// across all hardware threads.
static void copy_all_frames(CopyFn copy, uint8_t* buffer) {
  int num_threads = std::max(1u, std::thread::hardware_concurrency());

  for (size_t base = 0; base < g_frame_count; base += CHUNK) {
    int chunk = (int)std::min(CHUNK, g_frame_count - base);
    int frames_per_thread = chunk / num_threads;

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; ++t) {
      int start = t * frames_per_thread;
      int end = (t == num_threads - 1) ? chunk : start + frames_per_thread;
      if (start >= end) continue;

      workers.emplace_back([=] {
        for (int i = start; i < end; ++i) {
          uint8_t* dest = buffer + (base + i) * g_aligned_frame_size;
          copy(dest, g_source[i % g_source.size()].data, g_frame_size);
        }
      });
    }
    for (auto& th : workers) th.join();
  }
}

static uint8_t* alloc_buffer(bool huge, size_t* out_size) {
  size_t align = huge ? HUGE_PAGE : ALIGN;
  size_t size = (g_aligned_frame_size * g_frame_count + align - 1) & ~(align - 1);
  uint8_t* buf = static_cast<uint8_t*>(std::aligned_alloc(align, size));
  if (buf && huge) madvise(buf, size, MADV_HUGEPAGE);
  *out_size = size;
  return buf;
}

// ── benchmarks ──────────────────────────────────────────────────────────

static void BM_DecoderFull(benchmark::State& state) {
  for (auto _ : state) {
    SPP_STRUCTS::VideoData* vd = CPU_video_decoder(g_video_path);
    if (!vd) {
      state.SkipWithError("CPU_video_decoder() returned nullptr");
      break;
    }

    state.PauseTiming();
    SPP_UTILS::free_video_data(vd);
    state.ResumeTiming();
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * g_frame_count);
}

static void BM_DecodeOnly(benchmark::State& state) {
  for (auto _ : state) {
    cv::VideoCapture cap(g_video_path);
    cv::Mat frame;
    size_t n = 0;
    while (cap.read(frame)) ++n;
    benchmark::DoNotOptimize(n);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * g_frame_count);
}

// Fresh buffer every iteration: allocation + first-touch page faults + copy.
static void BM_CopyFresh(benchmark::State& state, CopyFn copy, bool huge) {
  for (auto _ : state) {
    size_t size = 0;
    uint8_t* buf = alloc_buffer(huge, &size);
    if (!buf) {
      state.SkipWithError("aligned_alloc failed");
      break;
    }

    copy_all_frames(copy, buf);
    benchmark::ClobberMemory();

    state.PauseTiming();
    std::free(buf);
    state.ResumeTiming();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * g_frame_size * g_frame_count);
}

// Buffer is written once before timing, so no page faults are measured.
static void BM_CopyPrefaulted(benchmark::State& state, CopyFn copy) {
  size_t size = 0;
  uint8_t* buf = alloc_buffer(false, &size);
  if (!buf) {
    state.SkipWithError("aligned_alloc failed");
    return;
  }
  std::memset(buf, 0, size);

  for (auto _ : state) {
    copy_all_frames(copy, buf);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * g_frame_size * g_frame_count);
  std::free(buf);
}

// ── CLI ──────────────────────────────────────────────────────────────────

static void usage(const char* prog) {
  std::fprintf(stderr,
      "usage:\n"
      "  %s --video <path> [google benchmark flags...]\n"
      "  %s --synthetic WIDTH HEIGHT FRAME_COUNT [google benchmark flags...]\n"
      "\n"
      "google benchmark flags (pass-through), e.g.:\n"
      "  --benchmark_repetitions=9\n"
      "  --benchmark_report_aggregates_only=true\n"
      "  --benchmark_filter=copy/\n",
      prog, prog);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }

  std::string mode;
  int syn_width = 0, syn_height = 0, syn_count = 0;

  std::vector<char*> passthrough_args;
  passthrough_args.push_back(argv[0]);

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];

    if (a == "--video" && i + 1 < argc) {
      mode = "video";
      g_video_path = argv[++i];
      continue;
    }
    if (a == "--synthetic" && i + 3 < argc) {
      mode = "synthetic";
      syn_width  = std::atoi(argv[++i]);
      syn_height = std::atoi(argv[++i]);
      syn_count  = std::atoi(argv[++i]);
      continue;
    }
    if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    }

    passthrough_args.push_back(argv[i]);
  }

  if (mode.empty()) {
    usage(argv[0]);
    return 2;
  }

  if (mode == "synthetic") {
    g_video_path = (std::filesystem::temp_directory_path() /
        ("spp_bench_decoder_" + std::to_string(syn_width) + "x" +
         std::to_string(syn_height) + "_" + std::to_string(syn_count) + ".mp4")).string();
    if (!write_synthetic_clip(g_video_path, syn_width, syn_height, syn_count)) return 1;
  }

  // ── read the clip once to get the real frame count and the copy source ──

  {
    cv::VideoCapture cap(g_video_path);
    if (!cap.isOpened()) {
      std::fprintf(stderr, "error: cannot open '%s'\n", g_video_path.c_str());
      return 1;
    }

    cv::Mat frame;
    while (cap.read(frame)) {
      if (g_source.size() < CHUNK) g_source.push_back(frame.clone());
      ++g_frame_count;
    }
  }
  if (g_frame_count == 0) {
    std::fprintf(stderr, "error: '%s' has no frames\n", g_video_path.c_str());
    return 1;
  }

  g_frame_size = g_source[0].total() * g_source[0].elemSize();
  g_aligned_frame_size = (g_frame_size + ALIGN - 1) & ~(ALIGN - 1);

  std::printf("── input ──\n");
  std::printf("  video           : %s\n", g_video_path.c_str());
  std::printf("  frames          : %zu  (%dx%d)\n", g_frame_count, g_source[0].cols, g_source[0].rows);
  std::printf("  buffer size     : %.2f MB\n", g_aligned_frame_size * g_frame_count / (1024.0 * 1024.0));
  std::printf("── system info ──\n");
  std::printf("  cpu model       : %s\n", cpu_model_name().c_str());
  std::printf("  hardware threads: %u\n", std::thread::hardware_concurrency());
  std::printf("  transparent huge pages: %s\n\n",
      read_first_line("/sys/kernel/mm/transparent_hugepage/enabled").c_str());

  // ── hand off to Google Benchmark ────────────────────────────────────────

  // The work runs on worker threads, so wall-clock time is what matters.
  std::vector<benchmark::Benchmark*> benches = {
    benchmark::RegisterBenchmark("decoder/full", BM_DecoderFull),
    benchmark::RegisterBenchmark("decoder/decode_only", BM_DecodeOnly),
    benchmark::RegisterBenchmark("copy/memcpy/4KB", BM_CopyFresh, copy_memcpy, false),
    benchmark::RegisterBenchmark("copy/asm/4KB", BM_CopyFresh, copy_asm, false),
    benchmark::RegisterBenchmark("copy/memcpy/2MB", BM_CopyFresh, copy_memcpy, true),
    benchmark::RegisterBenchmark("copy/asm/2MB", BM_CopyFresh, copy_asm, true),
    benchmark::RegisterBenchmark("copy/memcpy/prefaulted", BM_CopyPrefaulted, copy_memcpy),
    benchmark::RegisterBenchmark("copy/asm/prefaulted", BM_CopyPrefaulted, copy_asm),
  };
  for (auto* b : benches) b->Unit(benchmark::kMillisecond)->UseRealTime();

  int bench_argc = static_cast<int>(passthrough_args.size());
  benchmark::Initialize(&bench_argc, passthrough_args.data());
  if (benchmark::ReportUnrecognizedArguments(bench_argc, passthrough_args.data())) {
    usage(argv[0]);
    return 2;
  }

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
