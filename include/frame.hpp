#include <cstdint>
#include <cstddef>

struct Frame 
{
  int width;
  int height;
  int channels = 3; // RGB
  size_t size;      // width * height * channels
  
  // RGB data of frame
  uint8_t* data = nullptr;
  
  // A Flag that represents data's location
  enum class Location {Host, DEVICE};
  Location location;


};
