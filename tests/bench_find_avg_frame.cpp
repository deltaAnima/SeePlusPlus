//   REAL:      ./bench_find_avg_frame --video path/to/video.mp4 [--output path.png] [-- <google benchmark flags>]
//   SYNTHETIC: ./bench_find_avg_frame --synthetic WIDTH HEIGHT FRAME_COUNT [--output path.png] [-- <google benchmark flags>]
//
// Input selection (--video / --synthetic / --output) is handled by this
// program; everything else (repetitions, output format, filters, min-time,
// ...) is a native Google Benchmark flag, e.g.:
//
//   ./bench_find_avg_frame --video clip.mp4 \
//       --benchmark_repetitions=10 \
//       --benchmark_report_aggregates_only=true \
//       --benchmark_out=results/bench.json --benchmark_out_format=json

#include "SPP_STRUCTS.hpp"
#include "SPP_UTILS.hpp"

#include "decoder.hpp"
#include "FindAvgFrame.hpp"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static std::string cpu_model_name() {
#if defined(__linux__)
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
#endif
  return "unknown";
}

// Saves a FrameData's pixel buffer to disk as an image via OpenCV.
// FrameData::data has no per-row padding (frame_size = width*height*channels,
// fully contiguous), so it maps directly onto a cv::Mat with no copying of
// rows needed -- just wrap the pointer.
// Check https://docs.opencv.org/4.13.0/d3/d63/classcv_1_1Mat.html to learn
// how cv::Mat work
static bool save_frame_as_image(const SPP_STRUCTS::FrameData* fd,
    const std::string& path) {
  int cv_type;
  if (fd->channels == 3) {
    cv_type = CV_8UC3;
  } else if (fd->channels == 1) {
    cv_type = CV_8UC1;
  } else {
    std::fprintf(stderr,
        "warning: cannot save image, unsupported channel count %u\n",
        fd->channels);
    return false;
  }

  cv::Mat img(fd->height, fd->width, cv_type, fd->data);
  if (!cv::imwrite(path, img)) {
    std::fprintf(stderr, "warning: cv::imwrite failed for '%s'\n", path.c_str());
    return false;
  }

  std::printf("  saved average frame -> %s (%dx%d, %u channel%s)\n",
      path.c_str(), fd->width, fd->height, fd->channels,
      fd->channels == 1 ? "" : "s");
  return true;
}

static void free_frame(SPP_STRUCTS::FrameData* fd) {
  if (fd) {
    if (fd->data) std::free(fd->data);
    delete fd;
  }
}

// ── synthetic VideoData builder (no OpenCV / disk I/O) ─────────────────────

static SPP_STRUCTS::VideoData* make_synthetic_video(unsigned short width,
    unsigned short height,
    uint8_t channels,
    size_t count) {
  std::printf("→ allocating synthetic video buffer ...\n");

  SPP_STRUCTS::VideoData* vd = new SPP_STRUCTS::VideoData();
  vd->width    = width;
  vd->height   = height;
  vd->channels = channels;
  vd->count    = count;
  vd->fps      = 30.0;
  vd->duration = count / 30.0;

  const size_t frame_size = (size_t)width * height * channels;
  constexpr size_t ALIGN  = 64;
  const size_t aligned_frame_size = (frame_size + ALIGN - 1) & ~(ALIGN - 1);
  const size_t total_size = aligned_frame_size * count;

  vd->frame_size         = frame_size;
  vd->aligned_frame_size = aligned_frame_size;
  vd->total_alloc_size   = total_size;

  std::printf("  frame_size=%zu  aligned_frame_size=%zu  total=%.2f MB\n",
      frame_size, aligned_frame_size, total_size / (1024.0 * 1024.0));

  auto t0 = Clock::now();
  vd->data = static_cast<uint8_t*>(std::aligned_alloc(ALIGN, total_size));
  if (!vd->data) {
    std::fprintf(stderr, "error: failed to allocate %.2f GB for synthetic video\n",
        total_size / (1024.0 * 1024.0 * 1024.0));
    delete vd;
    return nullptr;
  }
  std::printf("  aligned_alloc took %.2f ms\n", ms_since(t0));

  std::printf("→ filling buffer with deterministic pseudo-random data (seed=42) ...\n");
  t0 = Clock::now();

  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, 255);
  for (size_t i = 0; i < total_size; ++i) {
    vd->data[i] = static_cast<uint8_t>(dist(rng));
  }
  std::printf("  fill took %.2f ms\n\n", ms_since(t0));

  return vd;
}

// ── global fixture state ────────────────────────────────────────────────
//
// The video buffer is decoded/generated once, outside any timed region --
// Google Benchmark's `for (auto _ : state)` loop should only measure
// find_average_frame() itself. The output PNG (if requested) is written
// once too, on the very first iteration; every iteration produces the same
// result so there's no point re-saving it.

static SPP_STRUCTS::VideoData* g_video       = nullptr;
static std::string             g_output_path;
static bool                    g_saved_output = false;

static void BM_FindAverageFrame(benchmark::State& state) {
  for (auto _ : state) {
    SPP_STRUCTS::FrameData* fd = find_average_frame(g_video);

    if (!fd) {
      state.SkipWithError("find_average_frame() returned nullptr");
      break;
    }

    // Saving to disk and freeing the result aren't part of what we're
    // measuring -- pause the clock around them.
    state.PauseTiming();
    if (!g_saved_output && !g_output_path.empty()) {
      save_frame_as_image(fd, g_output_path);
      g_saved_output = true;
    }
    free_frame(fd);
    state.ResumeTiming();
  }

  const size_t bytes_per_iter = g_video->frame_size * g_video->count;
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * bytes_per_iter);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * g_video->count);
}

// ── CLI ──────────────────────────────────────────────────────────────────

static void usage(const char* prog) {
  std::fprintf(stderr,
      "usage:\n"
      "  %s --video <path> [--output path.png] [google benchmark flags...]\n"
      "  %s --synthetic WIDTH HEIGHT FRAME_COUNT [--output path.png] [google benchmark flags...]\n"
      "\n"
      "google benchmark flags (pass-through), e.g.:\n"
      "  --benchmark_repetitions=10\n"
      "  --benchmark_report_aggregates_only=true\n"
      "  --benchmark_out=results.json --benchmark_out_format=json\n",
      prog, prog);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }

  std::string mode;
  std::string video_path;
  unsigned short syn_width = 0, syn_height = 0;
  size_t syn_count = 0;

  // Everything we don't recognize gets forwarded to
  // benchmark::Initialize() untouched, so all --benchmark_* flags keep
  // working exactly as documented upstream.
  std::vector<char*> passthrough_args;
  passthrough_args.push_back(argv[0]);

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];

    if (a == "--video" && i + 1 < argc) {
      mode = "video";
      video_path = argv[++i];
      continue;
    }
    if (a == "--synthetic" && i + 3 < argc) {
      mode = "synthetic";
      syn_width  = static_cast<unsigned short>(std::atoi(argv[++i]));
      syn_height = static_cast<unsigned short>(std::atoi(argv[++i]));
      syn_count  = static_cast<size_t>(std::atoll(argv[++i]));
      continue;
    }
    if (a == "--output" && i + 1 < argc) {
      g_output_path = argv[++i];
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

  // ── build/decode the input once, outside the timed region ──────────────

  if (mode == "video") {
    std::printf("── REAL mode ──\n");
    std::printf("→ decoding %s ...\n", video_path.c_str());

    auto t0 = Clock::now();
    g_video = CPU_video_decoder(video_path);
    double decode_ms = ms_since(t0);

    if (!g_video) {
      std::fprintf(stderr, "error: CPU_video_decoder returned nullptr\n");
      return 1;
    }

    std::printf("  decoded: %ux%u  channels=%u  %zu frames  fps=%.2f\n",
        g_video->width, g_video->height, g_video->channels, g_video->count,
        g_video->fps);
    std::printf("  frame_size=%zu bytes  aligned_frame_size=%zu bytes  "
        "total_alloc=%.2f MB\n",
        g_video->frame_size, g_video->aligned_frame_size,
        g_video->total_alloc_size / (1024.0 * 1024.0));
    std::printf("  decode time: %.2f ms  (%.2f MB/s, %.2f frames/s)\n\n",
        decode_ms,
        (g_video->total_alloc_size / (1024.0 * 1024.0)) / (decode_ms / 1000.0),
        g_video->count / (decode_ms / 1000.0));
  } else {
    std::printf("── SYNTHETIC mode: %ux%u  %zu frames ──\n", syn_width,
        syn_height, syn_count);
    g_video = make_synthetic_video(syn_width, syn_height, 3, syn_count);
    if (!g_video) return 1;
  }

  std::printf("── system info ──\n");
  std::printf("  cpu model       : %s\n", cpu_model_name().c_str());
  std::printf("  hardware threads: %u\n", std::thread::hardware_concurrency());
  std::printf("  bytes per run   : %zu (%.2f MB)\n\n",
      g_video->frame_size * g_video->count,
      (g_video->frame_size * g_video->count) / (1024.0 * 1024.0));

  // ── hand off to Google Benchmark ────────────────────────────────────────

  benchmark::RegisterBenchmark("find_average_frame", BM_FindAverageFrame)
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();

  int bench_argc = static_cast<int>(passthrough_args.size());
  benchmark::Initialize(&bench_argc, passthrough_args.data());
  if (benchmark::ReportUnrecognizedArguments(bench_argc, passthrough_args.data())) {
    usage(argv[0]);
    return 2;
  }

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();

  SPP_UTILS::free_video_data(g_video);
  return 0;
}
