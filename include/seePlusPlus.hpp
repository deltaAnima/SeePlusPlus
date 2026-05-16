#ifndef SEE_PLUS_PLUS_HPP
#define SEE_PLUS_PLUS_HPP

//custom
#include "decoder.hpp"
#include "SPP_STRUCTS.hpp"
#include "SPP_UTILS.hpp"

// 3rd party
#include <cstddef>
#include <string>
#include <cstdlib>


class SeePlusPlus
{
private:
  SPP_STRUCTS::VideoData* find_average_frame(SPP_STRUCTS::VideoData* video)
  {
    return nullptr;
  }

  size_t KNN_da_frame(SPP_STRUCTS::VideoData* video, SPP_STRUCTS::VideoData* average_frame)
  {
    return 1;
  }

public:
  std::string frameHunt(std::string path)
  {
    // decode -> average -> knn -> return 
    
    // 1. decode
    SPP_STRUCTS::VideoData* video_raw = CPU_video_decoder(path);

    // 2. find average frame TODO: Make a dedicated struct for avg frame
    SPP_STRUCTS::VideoData* average_frame = this->find_average_frame(video_raw);

    // 3. find the frame that is the most similar to the average_frame in the video
    size_t idx_of_sim_frame = this->KNN_da_frame(video_raw, average_frame);

    // 4. return
    //TODO: IDK yet
    return std::string ("yipee!");
  }
};

#endif    // SEE_PLUS_PLUS_HPP
