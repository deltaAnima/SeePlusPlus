#include "../include/seePlusPlus.hpp"
#include "../include/KNN.hpp"

SPP_STRUCTS::FrameData* SeePlusPlus::find_average_frame(SPP_STRUCTS::VideoData* video)
{
  return ::find_average_frame(video);
}

size_t SeePlusPlus::KNN_da_frame(SPP_STRUCTS::VideoData* video, SPP_STRUCTS::FrameData* average_frame)
{
  return findSmallestErr(average_frame, *video);
}

SPP_STRUCTS::FrameData* SeePlusPlus::frameHunt(std::string path)
{
  SPP_STRUCTS::VideoData* decoded_vid = CPU_video_decoder(path, false);
  
  SPP_STRUCTS::FrameData* avg_frame   = find_average_frame(decoded_vid); 
  
  size_t similar_idx = KNN_da_frame(decoded_vid, avg_frame);
  
  // AUGUST's ASSINMENT
  // Make this function return the FrameData of the similar_idx

  return nullptr;
}
