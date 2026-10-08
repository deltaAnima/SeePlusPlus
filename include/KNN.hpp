#ifndef KNN_HPP
#define KNN_HPP

#include "SPP_STRUCTS.hpp"

#include <cstddef>
#include <cstdint>

/*
 * @function compute_l1_distance_avx256
 * @brief L1 distance (sum of absolute differences) between two byte buffers,
 *        using AVX2 _mm256_sad_epu8 for 32 bytes per step
 * */
uint64_t compute_l1_distance_avx256(const uint8_t* a, const uint8_t* b, size_t size);

/*
 * @function compute_l1_distance_scalar
 * @brief Plain reference version of compute_l1_distance_avx256 (for tests and
 *        benchmarks)
 * */
uint64_t compute_l1_distance_scalar(const uint8_t* a, const uint8_t* b, size_t size);

/*
 * @function findSmallestErr
 * @brief returns the index of the frame in `video` with the smallest L1
 *        distance to `median`
 *
 * @return size_t   frame index (0-based). On a tie, the lowest index wins.
 *                  Returns SIZE_MAX when an input is null or empty.
 * */
size_t findSmallestErr(const SPP_STRUCTS::FrameData* median, const SPP_STRUCTS::VideoData& video);

#endif // KNN_HPP
