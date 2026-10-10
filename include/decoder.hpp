#pragma once
#include "SPP_STRUCTS.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

SPP_STRUCTS::VideoData* CPU_video_decoder(const std::string &path, const bool &isStream = false);

// AVX2 non-temporal copy used by the decoder. dest must be 32-byte aligned.
extern "C" void ASM_streaming_copy_avx2(uint8_t* dest, const uint8_t* src, size_t bytes);
