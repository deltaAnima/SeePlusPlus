#include <string>
#include <algorithm>
#include <vector>
#include "frame.hpp"

class VideoDecoder 
{
public:
    virtual ~VideoDecoder() = default;

    virtual bool open(const std::string& filename) = 0;

    virtual bool decodeNextFrame(Frame& frame) = 0;

    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
};
