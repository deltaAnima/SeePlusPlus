// Benchmark for the KNN stage on synthetic frames (no OpenCV / video needed).
// Written by Claude (AI assistant).
//
//   ./bench_knn [<google benchmark flags>]
//
// Compares:
//   - L1 distance of one frame:   scalar reference  vs  AVX2 (_mm256_sad_epu8)
//   - full closest-frame search:  scalar, 1 thread  vs  findSmallestErr (AVX2 + OpenMP)
//
// Note: the scalar reference is plain C++ compiled with the same flags, so the
// compiler is free to auto-vectorize it. The speedup shown is over what the
// compiler already produces, not over an artificially slow loop.

#include "SPP_STRUCTS.hpp"

#include "KNN.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <new>
#include <random>

constexpr size_t ALIGN = 64;

struct Resolution { int width, height; const char* name; };

static constexpr Resolution RESOLUTIONS[] = {
  {1280,  720, "720p"},
  {1920, 1080, "1080p"},
  {3840, 2160, "4K"},
};

// 30 frames keeps the 4K buffer under 1 GB
constexpr size_t FRAME_COUNT = 30;

// Synthetic video + median frame, filled with seeded random bytes.
struct Fixture
{
  SPP_STRUCTS::VideoData vd{};
  SPP_STRUCTS::FrameData fd{};

  explicit Fixture(const Resolution& r)
  {
    const size_t frame_size = static_cast<size_t>(r.width) * r.height * 3;
    const size_t aligned    = (frame_size + ALIGN - 1) & ~(ALIGN - 1);

    vd.width = fd.width = static_cast<unsigned short>(r.width);
    vd.height = fd.height = static_cast<unsigned short>(r.height);
    vd.count              = FRAME_COUNT;
    vd.frame_size         = fd.frame_size = frame_size;
    vd.aligned_frame_size = fd.aligned_frame_size = aligned;
    vd.total_alloc_size   = aligned * FRAME_COUNT;

    vd.data = static_cast<uint8_t*>(::operator new(vd.total_alloc_size, std::align_val_t(ALIGN)));
    fd.data = static_cast<uint8_t*>(::operator new(aligned, std::align_val_t(ALIGN)));

    std::mt19937 rng(42);
    for (size_t i = 0; i < vd.total_alloc_size; ++i) vd.data[i] = static_cast<uint8_t>(rng());
    for (size_t i = 0; i < aligned; ++i)             fd.data[i] = static_cast<uint8_t>(rng());
  }

  ~Fixture()
  {
    ::operator delete(vd.data, std::align_val_t(ALIGN));
    ::operator delete(fd.data, std::align_val_t(ALIGN));
  }
};

static Fixture& fixture(int res_idx)
{
  // built on first use so only the resolutions being run are allocated
  switch (res_idx)
  {
    case 0:  { static Fixture f(RESOLUTIONS[0]); return f; }
    case 1:  { static Fixture f(RESOLUTIONS[1]); return f; }
    default: { static Fixture f(RESOLUTIONS[2]); return f; }
  }
}

// ── one frame ──────────────────────────────────────────────────────────

static void BM_L1_Scalar(benchmark::State& state)
{
  Fixture& f = fixture(static_cast<int>(state.range(0)));
  for (auto _ : state)
    benchmark::DoNotOptimize(compute_l1_distance_scalar(f.fd.data, f.vd.data, f.fd.frame_size));
  state.SetBytesProcessed(state.iterations() * f.fd.frame_size * 2);
  state.SetLabel(RESOLUTIONS[state.range(0)].name);
}

static void BM_L1_AVX2(benchmark::State& state)
{
  Fixture& f = fixture(static_cast<int>(state.range(0)));
  for (auto _ : state)
    benchmark::DoNotOptimize(compute_l1_distance_avx256(f.fd.data, f.vd.data, f.fd.frame_size));
  state.SetBytesProcessed(state.iterations() * f.fd.frame_size * 2);
  state.SetLabel(RESOLUTIONS[state.range(0)].name);
}

// ── whole video ────────────────────────────────────────────────────────

static void BM_Search_ScalarSingleThread(benchmark::State& state)
{
  Fixture& f = fixture(static_cast<int>(state.range(0)));
  for (auto _ : state)
  {
    size_t   best_idx = 0;
    uint64_t best_err = UINT64_MAX;
    for (size_t i = 0; i < f.vd.count; ++i)
    {
      uint64_t err = compute_l1_distance_scalar(
          f.fd.data, f.vd.data + i * f.vd.aligned_frame_size, f.fd.frame_size);
      if (err < best_err) { best_err = err; best_idx = i; }
    }
    benchmark::DoNotOptimize(best_idx);
  }
  state.SetBytesProcessed(state.iterations() * f.fd.frame_size * f.vd.count);
  state.SetLabel(RESOLUTIONS[state.range(0)].name);
}

static void BM_Search_FindSmallestErr(benchmark::State& state)
{
  Fixture& f = fixture(static_cast<int>(state.range(0)));
  for (auto _ : state)
    benchmark::DoNotOptimize(findSmallestErr(&f.fd, f.vd));
  state.SetBytesProcessed(state.iterations() * f.fd.frame_size * f.vd.count);
  state.SetLabel(RESOLUTIONS[state.range(0)].name);
}

BENCHMARK(BM_L1_Scalar)->DenseRange(0, 2)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_L1_AVX2)->DenseRange(0, 2)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Search_ScalarSingleThread)->DenseRange(0, 2)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK(BM_Search_FindSmallestErr)->DenseRange(0, 2)->Unit(benchmark::kMillisecond)->UseRealTime();
