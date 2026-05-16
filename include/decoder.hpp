#pragma once
#include "SPP_STRUCTS.hpp"
#include <string>

SPP_STRUCTS::VideoData* CPU_video_decoder(const std::string &path, const bool &isStream = false);
