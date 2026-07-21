// Standalone unit test for find_average_frame(), built on GoogleTest.

#include "SPP_STRUCTS.hpp"
#include "SPP_UTILS.hpp"

#include "FindAvgFrame.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <functional>

// ── helpers for building synthetic VideoData ────────────────────────────

// Builds a VideoData whose per-frame pixel bytes are produced by `fill`,
// using the same 64-byte aligned_alloc layout CPU_video_decoder uses.
static SPP_STRUCTS::VideoData* make_video(
    unsigned short width, unsigned short height, uint8_t channels,
    size_t count,
    const std::function<uint8_t(size_t frame_idx, size_t pixel_idx)>& fill)
{
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

  vd->data = static_cast<uint8_t*>(std::aligned_alloc(ALIGN, total_size));

  for (size_t f = 0; f < count; ++f) {
    uint8_t* frame_ptr = vd->data + f * aligned_frame_size;

    for (size_t p = 0; p < frame_size; ++p) {
      frame_ptr[p] = fill(f, p);
    }

    // Zero the padding region so tests stay deterministic.
    if (aligned_frame_size > frame_size) {
      std::memset(frame_ptr + frame_size, 0, aligned_frame_size - frame_size);
    }
  }

  return vd;
}

static void free_video(SPP_STRUCTS::VideoData* vd) {
  SPP_UTILS::free_video_data(vd);
}

static void free_frame(SPP_STRUCTS::FrameData* fd) {
  if (fd) {
    if (fd->data) std::free(fd->data);
    delete fd;
  }
}

// A tiny RAII wrapper so ASSERT_* early-returns (which GTest requires to be
// void-returning) don't leak the VideoData/FrameData in each test.
struct VideoGuard {
  SPP_STRUCTS::VideoData* vd;
  explicit VideoGuard(SPP_STRUCTS::VideoData* v) : vd(v) {}
  ~VideoGuard() { free_video(vd); }
};

struct FrameGuard {
  SPP_STRUCTS::FrameData* fd;
  explicit FrameGuard(SPP_STRUCTS::FrameData* f) : fd(f) {}
  ~FrameGuard() { free_frame(fd); }
};

// ── tests ────────────────────────────────────────────────────────────────

TEST(FindAverageFrame, NullVideoPointerReturnsNull) {
  SPP_STRUCTS::FrameData* fd = find_average_frame(nullptr);
  EXPECT_EQ(fd, nullptr);
}

TEST(FindAverageFrame, NullVideoDataReturnsNull) {
  SPP_STRUCTS::VideoData vd{};
  vd.width  = 2;
  vd.height = 2;
  vd.channels = 3;
  vd.count = 5;
  vd.frame_size = 12;
  vd.aligned_frame_size = 64;
  vd.data = nullptr;   // <-- the thing under test

  SPP_STRUCTS::FrameData* fd = find_average_frame(&vd);
  EXPECT_EQ(fd, nullptr);
}

TEST(FindAverageFrame, ZeroCountReturnsNull) {
  // count == 0 means make_video's loop never touches the buffer, so this
  // just exercises allocation/free; the real assertion is on vd2 below.
  SPP_STRUCTS::VideoData* vd = make_video(2, 2, 3, /*count=*/0,
      [](size_t, size_t) -> uint8_t { return 0; });
  free_video(vd);

  SPP_STRUCTS::VideoData vd2{};
  vd2.width  = 2;
  vd2.height = 2;
  vd2.channels = 3;
  vd2.count = 0;
  vd2.frame_size = 12;
  vd2.aligned_frame_size = 64;
  uint8_t dummy[64] = {0};
  vd2.data = dummy;

  SPP_STRUCTS::FrameData* fd = find_average_frame(&vd2);
  EXPECT_EQ(fd, nullptr);
}

TEST(FindAverageFrame, ConstantValueAverageIsExact) {
  // 10 frames, every pixel = 42 everywhere -> average must be exactly 42.
  const size_t count = 10;
  SPP_STRUCTS::VideoData* vd = make_video(4, 4, 3, count,
      [](size_t, size_t) -> uint8_t { return 42; });
  VideoGuard vguard(vd);

  SPP_STRUCTS::FrameData* fd = find_average_frame(vd);
  ASSERT_NE(fd, nullptr);
  FrameGuard fguard(fd);

  ASSERT_EQ(fd->frame_size, vd->frame_size);

  for (size_t p = 0; p < fd->frame_size; ++p) {
    EXPECT_EQ(fd->data[p], 42) << "mismatch at pixel " << p;
  }
}

TEST(FindAverageFrame, ManyHighValueFramesDoNotOverflow) {
  // Regression test: with a naive uint8_t accumulator, summing 255 frames
  // of value 250 overflows before division. A correctly-widened
  // accumulator must still produce an average of exactly 250.
  const size_t count = 255;
  SPP_STRUCTS::VideoData* vd = make_video(2, 2, 3, count,
      [](size_t, size_t) -> uint8_t { return 250; });
  VideoGuard vguard(vd);

  SPP_STRUCTS::FrameData* fd = find_average_frame(vd);
  ASSERT_NE(fd, nullptr);
  FrameGuard fguard(fd);

  for (size_t p = 0; p < fd->frame_size; ++p) {
    EXPECT_EQ(fd->data[p], 250) << "mismatch at pixel " << p;
  }
}

TEST(FindAverageFrame, VaryingPixelValuesMatchReference) {
  // Non-trivial per-pixel/per-frame values. The expected result is
  // computed independently with a uint64_t accumulator so the reference
  // math can't overflow either.
  const size_t count = 7;
  SPP_STRUCTS::VideoData* vd = make_video(3, 2, 3, count,
      [](size_t f, size_t p) -> uint8_t {
        return static_cast<uint8_t>((f * 17 + p * 5) % 256);
      });
  VideoGuard vguard(vd);

  std::vector<uint64_t> expected_sum(vd->frame_size, 0);
  for (size_t f = 0; f < count; ++f) {
    for (size_t p = 0; p < vd->frame_size; ++p) {
      uint8_t val = static_cast<uint8_t>((f * 17 + p * 5) % 256);
      expected_sum[p] += val;
    }
  }

  SPP_STRUCTS::FrameData* fd = find_average_frame(vd);
  ASSERT_NE(fd, nullptr);
  FrameGuard fguard(fd);

  for (size_t p = 0; p < fd->frame_size; ++p) {
    uint8_t expected = static_cast<uint8_t>(expected_sum[p] / count);
    EXPECT_EQ(fd->data[p], expected) << "mismatch at pixel " << p;
  }
}

TEST(FindAverageFrame, PaddingBytesAreZeroed) {
  // Dimensions chosen so frame_size (27) is NOT a multiple of 64 — this
  // leaves a real padding region that find_average_frame() must zero.
  const size_t count = 3;
  SPP_STRUCTS::VideoData* vd = make_video(3, 3, 3, count,
      [](size_t, size_t) -> uint8_t { return 200; });
  VideoGuard vguard(vd);

  ASSERT_EQ(vd->frame_size, 27u);
  ASSERT_EQ(vd->aligned_frame_size, 64u);   // next multiple of 64 after 27

  SPP_STRUCTS::FrameData* fd = find_average_frame(vd);
  ASSERT_NE(fd, nullptr);
  FrameGuard fguard(fd);

  ASSERT_GT(fd->aligned_frame_size, fd->frame_size);

  for (size_t p = fd->frame_size; p < fd->aligned_frame_size; ++p) {
    EXPECT_EQ(fd->data[p], 0) << "padding byte " << p << " not zeroed";
  }
}

TEST(FindAverageFrame, ResultIs64ByteAligned) {
  SPP_STRUCTS::VideoData* vd = make_video(5, 5, 3, 4,
      [](size_t, size_t) -> uint8_t { return 100; });
  VideoGuard vguard(vd);

  SPP_STRUCTS::FrameData* fd = find_average_frame(vd);
  ASSERT_NE(fd, nullptr);
  FrameGuard fguard(fd);

  EXPECT_EQ(reinterpret_cast<uintptr_t>(fd->data) % 64, 0u);
}
