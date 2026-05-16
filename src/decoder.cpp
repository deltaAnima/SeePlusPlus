#include "SPP_STRUCTS.hpp"

#include <cstdint>
#include <ostream>
#include <thread>
#include <vector>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <immintrin.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>

#include <logger.hpp>

/*
 * Func Name: CPU_video_decoder
 * description: A function for decoding
 * */
SPP_STRUCTS::VideoData* CPU_video_decoder(const std::string &path,
                                        const bool &isStream = false)
{
  LOG_DEBUG("CPU_video_decoder will decode the video at :" << path);

  // 1. Opening the vid. TODO: Make it stream later. Use var isStream for it.
  cv::VideoCapture cap(path);
  // 1-1. Cheack it does it found video
  if(!cap.isOpened())
  {
    LOG_ERR("Video at " << path << "cannot be found!!");
    return nullptr;
  }

  // 2. Allocate memory
  // 2-1. meta data of vid
  int width   = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
  int height  = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
  size_t frame_size = (size_t)width * height * 3;
  size_t count = (size_t)cap.get(cv::CAP_PROP_FRAME_COUNT);
  double fps = cap.get(cv::CAP_PROP_FPS);
  double duration = (fps > 0) ? (static_cast<double>(count) / fps) : 0.0;

  // 2-2 aligned_alloc
  size_t aligned_cache_size = 64;   // general size of cache lane
  size_t aligned_frame_size = (frame_size + aligned_cache_size - 1) & ~(aligned_cache_size - 1);   // beacuse total_size can be not a multiples of 64, make it fit.
  size_t total_size = aligned_frame_size * count;

  // 2-3. actual allocation
  void* ptr_aligen = std::aligned_alloc(aligned_cache_size, total_size);
  uint8_t* raw_ptr_data = static_cast<uint8_t*>(ptr_aligen);

  // 2-4. check is it allocated properly
  if(!raw_ptr_data)
  {
    LOG_ERR("Memory allocation failed!");
    return nullptr;
  }
  LOG_DEBUG("Allocated (Aligned): " << total_size / (1024 * 1024) << " MB");

  // 3. EXTRACTING VIDEO DATA
  int frame_idx = 0;
  int chunk_size = 64;
  std::vector<cv::Mat> chunk_frames(chunk_size);

  while (true) 
  {
    int read_count = 0;

    for (int i = 0; i < chunk_size; ++i) 
    {
      if (cap.read(chunk_frames[i]))
      {
        read_count++;
      }
      else
      {
        break;
      }
    }

    if (read_count == 0) 
    {
      break;
    }

    // MULTITHREADING
    std::vector<std::thread> workers;
    int num_threads = std::thread::hardware_concurrency();
    int frames_per_thread = read_count / num_threads;

    auto worker_func = [&](int start_chunk_idx, int end_chunk_idx) 
    {
      for (int i = start_chunk_idx; i < end_chunk_idx; ++i) 
      {
        uint8_t* dest = raw_ptr_data + (size_t)(frame_idx + i) * aligned_frame_size;

        std::memcpy(dest, chunk_frames[i].data, frame_size);
      }
    };

    for (int t = 0; t < num_threads; ++t) 
    {
      int start = t * frames_per_thread;
      int end = (t == num_threads - 1) ? read_count : start + frames_per_thread;

      if (start < end) 
        workers.emplace_back(worker_func, start, end);
    }

    for (auto& th : workers) 
      th.join();

    frame_idx += read_count;
    LOG_DEBUG("Progress: " << frame_idx << "/" << count);
  }

  SPP_STRUCTS::VideoData* vd = new SPP_STRUCTS::VideoData();
  vd->width       = static_cast<unsigned short>(width);
  vd->height      = static_cast<unsigned short>(height);
  vd->count       = count;
  vd->fps         = fps;
  vd->duration    = duration;
  vd->frame_size  = frame_size;
  vd->aligned_frame_size  = aligned_frame_size;
  vd->total_alloc_size    = total_size;
  vd->data        = raw_ptr_data;

  return vd;
}

int main(int argc, char* argv[])
{
	bool verbose = false;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--verbose" || arg == "-v")   verbose = true;
		else if (arg == "--help" || arg == "-h") {
			std::cout <<
				"Usage: vn-discord-rpc [OPTIONS]\n\n"
				"Options:\n"
				"  -v, --verbose  Enable DEBUG-level logging\n"
				"  -h, --help     Show this message\n\n";
			return 0;
		}
	}

  if (verbose) {
    Logger::get().setLevel(LogLevel::DEBUG);
    LOG_INFO("Debug Verbose");
  }
}
