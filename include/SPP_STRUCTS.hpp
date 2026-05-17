#ifndef SPP_STRUCTS_HPP
#define SPP_STRUCTS_HPP

#include <cstddef>
#include <cstdint>


/*
 * @namespace SPP_STRUCTS
 * @brief A namespace to manage structs in SeePlusPlus proj.
 *
 * * @note 
 *       Current structs in this name space:
 *        - VideoData (contains data of extracted video data)
 *        - FrameData (contains data of a single aligned video frame)
 *
 * */
namespace SPP_STRUCTS
{
  /*
   * @struct VideoData
   * @brief A struct for saving aligned video frame data
   *
   * * @note 
   *      - The variable 'data' in this stuct is allocated with 'std::aligned_alloc'
   *      - YOU MUST CALL SPP_STRUCTS::free_video_data() OR CALL std::free() TO FREE 
   *        THE MEMORY. **NEVER USE 'delete'**
   *
   * */
  struct VideoData
  {
    // Basic info of data
    unsigned short width;
    unsigned short height;
    uint8_t channels = 3;     // RGB
    size_t count;             // Total frame number
    double fps;               // frame per sec
    double duration;          // total playtime

    // Memory layout information
    size_t frame_size;        // the actual size of data (wid * hei * channels)
    size_t aligned_frame_size;// size of a frame that contains align and padding
    size_t total_alloc_size;  // total alloc size
    
    // actual data pointer
    uint8_t* data = nullptr;
  };

  /*
  * @struct FrameData
  * @brief A struct for saving a single aligned video frame data
  * * * @note-
  * - The variable 'data' in this struct is allocated with 'std::aligned_alloc'
  * - YOU MUST CALL SPP_STRUCTS::free_frame_data() OR CALL std::free() TO FREE
  * THE MEMORY. **NEVER USE 'delete'**
  * */
  struct FrameData
  {
    // Basic info of data
    unsigned short width;
    unsigned short height;
    uint8_t channels = 3;       // RGB

    // Memory layout information
    size_t frame_size;          // the actual size of data (wid * hei * channels)
    size_t aligned_frame_size;  // size of a frame that contains align and padding
    
    // actual data pointer
    uint8_t* data = nullptr;
  };
}

#endif  // SPP_STRUCTS_HPP
