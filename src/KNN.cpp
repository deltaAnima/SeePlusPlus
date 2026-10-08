#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <immintrin.h>
#include <omp.h>
#include "../include/KNN.hpp"
#include "../include/SPP_STRUCTS.hpp"

uint64_t compute_l1_distance_avx256(const uint8_t* a, const uint8_t* b, size_t size)
{
  __m256i sum = _mm256_setzero_si256();

  size_t i = 0;

  // one __m256i = 32 bytes per step
  for(; i + 31 < size; i += 32)
  {
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i));

    // |a - b| summed into four 64-bit lanes
    __m256i sum_abs_diff = _mm256_sad_epu8(va, vb);

    sum = _mm256_add_epi64(sum, sum_abs_diff);
  }

  alignas(32) uint64_t buffer[4];
  _mm256_store_si256(reinterpret_cast<__m256i*>(buffer), sum);

  uint64_t total_l1 = buffer[0] + buffer[1] + buffer[2] + buffer[3];

  // leftover bytes that do not fill a full 32-byte step
  for(; i < size; i++)
  {
    total_l1 += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
  }

  return total_l1;
}

uint64_t compute_l1_distance_scalar(const uint8_t* a, const uint8_t* b, size_t size)
{
  uint64_t total_l1 = 0;

  for (size_t i = 0; i < size; i++)
  {
    total_l1 += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
  }

  return total_l1;
}

size_t findSmallestErr(const SPP_STRUCTS::FrameData* median, const SPP_STRUCTS::VideoData& video)
{
  if (!median || !median->data || !video.data || video.count == 0)
    return SIZE_MAX;

  size_t idx_similar = SIZE_MAX;
  uint64_t min_l1_err = UINT64_MAX;

  const size_t frame_size = median->frame_size;
  const uint8_t* median_data = median->data;

  #pragma omp parallel
  {
    uint64_t local_min_error = UINT64_MAX;
    size_t local_idx = SIZE_MAX;

    #pragma omp for nowait
    for (size_t i = 0; i < video.count; ++i)
    {
      const uint8_t* target_frame_data = video.data + (i * video.aligned_frame_size);

      uint64_t error_L1 = compute_l1_distance_avx256(median_data, target_frame_data, frame_size);

      if(error_L1 < local_min_error)
      {
        local_min_error= error_L1;
        local_idx = i;
      }
    }

    // lowest index wins a tie so the result does not depend on thread order
    #pragma omp critical
    {
      if(local_min_error < min_l1_err ||
         (local_min_error == min_l1_err && local_idx < idx_similar))
      {
        min_l1_err = local_min_error;
        idx_similar = local_idx;
      }
    }
  }

  return idx_similar;
}
