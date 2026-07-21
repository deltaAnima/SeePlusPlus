#ifndef SEE_PLUS_PLUS_HPP
#define SEE_PLUS_PLUS_HPP

//custom
#include "decoder.hpp"
#include "SPP_STRUCTS.hpp"
#include "SPP_UTILS.hpp"
#include "FindAvgFrame.hpp"

// 3rd party
#include <cstddef>
#include <string>
#include <cstdlib>


class SeePlusPlus
{
private:
  SPP_STRUCTS::FrameData* find_average_frame(SPP_STRUCTS::VideoData* video);

  size_t KNN_da_frame(SPP_STRUCTS::VideoData* video, SPP_STRUCTS::FrameData* average_frame);

public:
  SPP_STRUCTS::FrameData* frameHunt(std::string path);
};

#endif    // SEE_PLUS_PLUS_HPP
