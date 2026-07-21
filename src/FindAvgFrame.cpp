#include "SPP_STRUCTS.hpp"

#include "logger.hpp"
#include "FindAvgFrame.hpp"

#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
#include <omp.h>

// TODO: Highway or CUDA to make it run faster

SPP_STRUCTS::FrameData* find_average_frame(SPP_STRUCTS::VideoData* video)
{
  if (!video)
  {
    LOG_ERR("VideoData* pointer is null.");
    return nullptr;
  }

  if (!video->data)
  {
    LOG_ERR("VideoData data* is null.");
    return nullptr;
  }

  if (video->count ==0)
  {
    LOG_ERR("Frame number is 0. How?");
    return nullptr;
  }

  const size_t frame_size     = video->frame_size;
  const size_t count          = video->count;
  const size_t   aligned_frame_size   = video->aligned_frame_size;
  const uint8_t* base                 = video->data;

  // Accumulate
  // NOTE: uint32_t is used (not uint8_t) so the running sum cannot silently
  // wrap around once more than ~1-2 frames of pixel values are summed.
  std::vector<uint32_t> accumulate(frame_size, 0);

  uint32_t* acc_ptr = accumulate.data();

  constexpr size_t BLOCK = 4096;
  // sum each block across all frames in a small cache-resident buffer,
  // then write it out once -- avoids repeated read-modify-write on `accumulate`
  #pragma omp parallel
  {
    const int    nthreads = omp_get_num_threads();
    const int    tid      = omp_get_thread_num();
    const size_t chunk    = (frame_size + nthreads - 1) / nthreads;
    const size_t p_lo     = std::min(frame_size, static_cast<size_t>(tid) * chunk);
    const size_t p_hi     = std::min(frame_size, p_lo + chunk);

    uint32_t local_sum[BLOCK];

    for (size_t block_start = p_lo; block_start < p_hi; block_start += BLOCK)
    {
      const size_t block_end = std::min(p_hi, block_start + BLOCK);
      const size_t block_len = block_end - block_start;

      std::memset(local_sum, 0, block_len * sizeof(uint32_t));

      for (size_t i = 0; i < count; ++i)
      {
        const uint8_t* src = base + i * aligned_frame_size + block_start;

        #pragma omp simd
        for (size_t j = 0; j < block_len; ++j)
          local_sum[j] += static_cast<uint32_t>(src[j]);
      }

      std::memcpy(acc_ptr + block_start, local_sum, block_len * sizeof(uint32_t));
    }
  }

  // Allocate result
  // Round-up to multiplie of N
  constexpr size_t ALIGN          = 64;
  // ALIGN-byte alignment - e.g. frame_size 60 -> aligned_frame_size 64 ; frame_size 100 -> aligned_frame_size 128 ...
  const size_t result_aligned_frame_size = (frame_size + ALIGN - 1) & ~(ALIGN - 1);

  void*    raw = std::aligned_alloc(ALIGN, result_aligned_frame_size);
  uint8_t* dst = static_cast<uint8_t*>(raw);

  if (!dst)
  {
    LOG_ERR("dst pointer point to null");
    return nullptr;
  }

  // Divide and Store result
  #pragma omp parallel for schedule(static)
  for (size_t p = 0; p < frame_size; ++p)
    dst[p] = static_cast<uint8_t>(accumulate[p] / count);

  if (result_aligned_frame_size > frame_size)
    std::memset(dst + frame_size, 0, result_aligned_frame_size - frame_size);

  // Create FrameData and return
  SPP_STRUCTS::FrameData* fd = new SPP_STRUCTS::FrameData();
  fd->width                 = video->width;
  fd->height                = video->height;
  fd->channels              = video->channels;
  fd->frame_size            = video->frame_size;
  fd->aligned_frame_size    = video->aligned_frame_size;
  fd->data                  = dst;
  return fd;
}
