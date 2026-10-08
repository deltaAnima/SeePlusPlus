#include <cstddef>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <immintrin.h>
#include <omp.h>
#include "../include/KNN.hpp"
#include "../include/SPP_STRUCTS.hpp"

static inline uint64_t compute_l1_distance_avx256(const uint8_t* a, const uint8_t* b, size_t size)
{
  __m256i sum = _mm256_setzero_si256();

  size_t i = 0;

  for(; i + 63 < size; i += 64)
  {
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));

    __m256i sum_abs_diff = _mm256_sad_epu8(va, vb);

    sum = _mm256_add_epi64(va, vb);
  }
  
  alignas(32) uint64_t buffer[4];
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(buffer), sum);

  uint64_t total_l1 = buffer[0] + buffer[1] + buffer[2] + buffer[3];

  for(; i < size; i++)
  {
    total_l1 += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
  }

  return total_l1;
}

size_t findSmallestErr(SPP_STRUCTS::FrameData* median, SPP_STRUCTS::VideoData video)
{
  size_t idx_similar;
  uint64_t min_l1_err = UINT64_MAX;

  const size_t frame_size = median->frame_size;
  const uint8_t* median_data = median->data;

  #pragma omp parallel
  {
    uint64_t local_min_error = UINT64_MAX;
    size_t local_idx = 0;

    #pragma omp for nowait
    for (size_t i = 0; i < video.count; ++i)
    {
      const uint8_t* target_frame_data = video.data + (i * video.aligned_frame_size);

      double error_L1 = compute_l1_distance_avx256(median_data, target_frame_data, frame_size);

      if(error_L1 < local_min_error)
      {
        local_min_error= error_L1;
        local_idx = i;
      }
    }

    #pragma omp critical
    {
      if(local_min_error < min_l1_err)
      {
        min_l1_err = local_min_error;
        idx_similar = local_idx;
      }
    }
  }

  return idx_similar;
}

