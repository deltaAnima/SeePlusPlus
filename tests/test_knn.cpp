// Standalone unit test for the KNN stage (compute_l1_distance_avx256 and
// findSmallestErr), built on GoogleTest.
//
// Written by Claude (AI assistant).

#include "SPP_STRUCTS.hpp"

#include "KNN.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

// ── helpers ─────────────────────────────────────────────────────────────

constexpr size_t ALIGN = 64;

static uint8_t* alloc_aligned(size_t size)
{
  return static_cast<uint8_t*>(::operator new(size, std::align_val_t(ALIGN)));
}

static void free_aligned(uint8_t* p)
{
  ::operator delete(p, std::align_val_t(ALIGN));
}

// Owns a VideoData with the same 64-byte aligned layout CPU_video_decoder uses.
struct TestVideo
{
  SPP_STRUCTS::VideoData vd{};

  TestVideo(size_t frame_size, size_t count)
  {
    vd.count              = count;
    vd.frame_size         = frame_size;
    vd.aligned_frame_size = (frame_size + ALIGN - 1) & ~(ALIGN - 1);
    vd.total_alloc_size   = vd.aligned_frame_size * count;
    vd.data               = alloc_aligned(vd.total_alloc_size);
    std::memset(vd.data, 0, vd.total_alloc_size);
  }

  ~TestVideo() { free_aligned(vd.data); }

  uint8_t* frame(size_t idx) { return vd.data + idx * vd.aligned_frame_size; }
};

struct TestFrame
{
  SPP_STRUCTS::FrameData fd{};

  explicit TestFrame(size_t frame_size)
  {
    fd.frame_size         = frame_size;
    fd.aligned_frame_size = (frame_size + ALIGN - 1) & ~(ALIGN - 1);
    fd.data               = alloc_aligned(fd.aligned_frame_size);
    std::memset(fd.data, 0, fd.aligned_frame_size);
  }

  ~TestFrame() { free_aligned(fd.data); }
};

static std::vector<uint8_t> random_bytes(size_t size, uint32_t seed)
{
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<uint8_t> v(size);
  for (auto& b : v) b = static_cast<uint8_t>(dist(rng));
  return v;
}

// ── compute_l1_distance_avx256 ─────────────────────────────────────────

TEST(L1Distance, MatchesScalarForManySizes)
{
  // sizes around the 32-byte SIMD step, plus odd and frame-like sizes
  for (size_t size : {0, 1, 31, 32, 33, 63, 64, 65, 100, 1000, 4097, 1920 * 1080 * 3})
  {
    auto a = random_bytes(size, 1);
    auto b = random_bytes(size, 2);

    EXPECT_EQ(compute_l1_distance_avx256(a.data(), b.data(), size),
              compute_l1_distance_scalar(a.data(), b.data(), size))
        << "size=" << size;
  }
}

TEST(L1Distance, IdenticalBuffersAreZero)
{
  auto a = random_bytes(1000, 3);
  EXPECT_EQ(compute_l1_distance_avx256(a.data(), a.data(), a.size()), 0u);
}

TEST(L1Distance, MaxDifferenceDoesNotOverflow)
{
  // every byte differs by 255 -- checks the 64-bit accumulation
  const size_t size = 3840 * 2160 * 3;
  std::vector<uint8_t> zeros(size, 0), full(size, 255);

  EXPECT_EQ(compute_l1_distance_avx256(zeros.data(), full.data(), size),
            static_cast<uint64_t>(size) * 255);
}

TEST(L1Distance, IsSymmetric)
{
  auto a = random_bytes(777, 4);
  auto b = random_bytes(777, 5);

  EXPECT_EQ(compute_l1_distance_avx256(a.data(), b.data(), a.size()),
            compute_l1_distance_avx256(b.data(), a.data(), a.size()));
}

// ── findSmallestErr ─────────────────────────────────────────────────────

TEST(FindSmallestErr, FindsPlantedFrame)
{
  const size_t frame_size = 64 * 48 * 3 + 5;   // not a multiple of 64 -> padding
  const size_t count      = 50;
  const size_t planted    = 37;

  TestVideo video(frame_size, count);
  TestFrame median(frame_size);

  auto target = random_bytes(frame_size, 10);
  std::memcpy(median.fd.data, target.data(), frame_size);

  for (size_t i = 0; i < count; ++i)
  {
    auto noise = random_bytes(frame_size, 100 + static_cast<uint32_t>(i));
    std::memcpy(video.frame(i), noise.data(), frame_size);
  }
  // planted frame is the median with a single byte changed
  std::memcpy(video.frame(planted), target.data(), frame_size);
  video.frame(planted)[0] ^= 1;

  EXPECT_EQ(findSmallestErr(&median.fd, video.vd), planted);
}

TEST(FindSmallestErr, MatchesBruteForceScalar)
{
  const size_t frame_size = 320 * 240 * 3;
  const size_t count      = 30;

  TestVideo video(frame_size, count);
  TestFrame median(frame_size);

  auto m = random_bytes(frame_size, 7);
  std::memcpy(median.fd.data, m.data(), frame_size);
  for (size_t i = 0; i < count; ++i)
  {
    auto f = random_bytes(frame_size, 200 + static_cast<uint32_t>(i));
    std::memcpy(video.frame(i), f.data(), frame_size);
  }

  size_t   best_idx = 0;
  uint64_t best_err = UINT64_MAX;
  for (size_t i = 0; i < count; ++i)
  {
    uint64_t err = compute_l1_distance_scalar(median.fd.data, video.frame(i), frame_size);
    if (err < best_err) { best_err = err; best_idx = i; }
  }

  EXPECT_EQ(findSmallestErr(&median.fd, video.vd), best_idx);
}

TEST(FindSmallestErr, TieReturnsLowestIndex)
{
  // all frames identical -> every distance is equal
  const size_t frame_size = 1000;
  TestVideo video(frame_size, 64);
  TestFrame median(frame_size);

  EXPECT_EQ(findSmallestErr(&median.fd, video.vd), 0u);
}

TEST(FindSmallestErr, InvalidInputReturnsSizeMax)
{
  TestFrame median(100);
  TestVideo video(100, 1);

  SPP_STRUCTS::VideoData empty = video.vd;
  empty.count = 0;

  EXPECT_EQ(findSmallestErr(nullptr, video.vd), SIZE_MAX);
  EXPECT_EQ(findSmallestErr(&median.fd, empty), SIZE_MAX);
}
