#ifndef SPP_UTILS_HPP
#define SPP_UTILS_HPP

#include "SPP_STRUCTS.hpp"

#include <cstdint>
#include <cstdlib>

/*
 * @namespace SPP_UTILS
 * @brief A namespace for handing SPP_STRUCTS more easier and more maintainable
 * * @note 
 *      - Current functions
 *        - get_frame
 *        - time_to_index
 *        - get_frame_at_time
 *        - free_video_data
 * */
namespace SPP_UTILS
{
  /*
   * @function get_frame
   * @brief returns the beginning memory address of frame in the certain index
   * 
   * @param vd          Video pointer
   * @param idx         frame index that you trying to access (0-based)
   * 
   * @return uint8_t*   The beginning pointer of the frame data (when it fails, it returns nullptr)
   *
   * @note
   *  - It uses aligned_frame_size to safely jumps over padding area when accessing it
   * */
  inline uint8_t* get_frame(const SPP_STRUCTS::VideoData* vd, size_t idx)
  {
    if (!vd || !vd->data || idx >= vd->count)
      return nullptr;

    return vd->data + (idx * vd->aligned_frame_size);
  }

  /*
   * @function time_to_index
   * @brief calculate the nearest frame index based on input time(seconds)
   *
   * @param vd        Video pointer
   * @param seconds   playtime (unit: seconds)
   *
   * @return size_t   calculated frame index
   *
   * @note
   *  - when input time is over the range, it returns 0
   * */
  inline size_t time_to_index(const SPP_STRUCTS::VideoData* vd, double seconds) 
  {
    if (!vd || seconds < 0 || seconds >= vd->duration) 
      return 0;

    return static_cast<size_t>(seconds * vd->fps);
  }

  /*
   * @function get_frame_at_time
   * @brief Returns frame data address at the input time(seconds)
   *
   * @param vd          Video pointer
   * @param seconds     the time trying to get frame
   *
   * @return uint8_t*   the beginning pointer of the frame in that time (returns nullptr when it fails)
   * */
  inline uint8_t* get_frame_at_time(const SPP_STRUCTS::VideoData* vd, double seconds) 
  {
    size_t idx = time_to_index(vd, seconds);

    if (!vd->data || idx >= vd->count) 
      return nullptr;

    return vd->data + (idx * vd->aligned_frame_size);
  }

  /*
   * @function free_video_data
   * @brief safely unallocates the VideoData struct and interal aligned memory
   *
   * @param vd    A VideoData struct pointer to delete
   *
   * @warning  It will free the internal data that allocated with std::aligned_alloc first with std:free(),
   *           and then delete the struct
   * */
  inline void free_video_data(SPP_STRUCTS::VideoData* vd) 
  {
    if (vd) 
    {
      if (vd->data) 
        std::free(vd->data);
      delete vd;
    }
  }
}

#endif  // SPP_UTILS_HPP
