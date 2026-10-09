#include "SPP_STRUCTS.hpp"

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <thread>
#include <vector>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <immintrin.h>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <logger.hpp>



extern "C" void ASM_streaming_copy_avx2(uint8_t* dest, const uint8_t* src, size_t bytes) 
{
  size_t processed = bytes & ~(size_t)63;     // bytes rounded down to a multiple of 64
  __asm__ volatile(
    "xorq %%rcx, %%rcx\n\t"                   // Initialize byte offset (x86 scale can only be 1, 2, 4, 8, so rcx counts bytes, not chunks)

    "1:\n\t"
    "cmpq %[n], %%rcx\n\t"                    // Compare byte offset with processed
    "jae 2f\n\t"                              // unsigned compare (sizes are never negative)

    // 1. non-sorted loading: reads 64 bytes from src into ymm0 and ymm1 (OpenCV Mat original data is not sorted)
    "vmovdqu (%[s], %%rcx), %%ymm0\n\t"
    "vmovdqu 32(%[s], %%rcx), %%ymm1\n\t"

    // You can add any additional processing here if needed
    "prefetchnta 128(%[s], %%rcx)\n\t"       // prefetch 2 chunks ahead (rcx is already a byte offset, so no scale)

    // 2. save streaming data bypassing cache: writes 64 bytes from ymm0 and ymm1 to dest (dest must be 32-byte aligned)
    "vmovntdq %%ymm0, (%[d], %%rcx)\n\t"
    "vmovntdq %%ymm1, 32(%[d], %%rcx)\n\t"

    "addq $64, %%rcx\n\t"                     // Move to the next 64 bytes
    "jmp 1b\n\t"                              // Repeat the loop

    "2:\n\t"
    "sfence\n\t"
    "vzeroupper\n\t"

    :
    : [d] "r"(dest), [s] "r"(src), [n] "r"(processed)
    : "rcx", "xmm0", "xmm1", "memory", "cc"   // "cc": cmp/add change the flags
  );

  // copy the leftover bytes (less than 64) that the loop did not handle
  size_t remainder = bytes - processed;
  if (remainder > 0)
  {
    std::memcpy(dest + processed, src + processed, remainder);
  }
}

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
  int width  = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
  int height = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
  size_t count = (size_t)cap.get(cv::CAP_PROP_FRAME_COUNT);
  double fps = cap.get(cv::CAP_PROP_FPS);
  double duration = (fps > 0) ? (static_cast<double>(count) / fps) : 0.0;

  cv::Mat test_frame;
  if(!cap.read(test_frame) || count == 0)
  {
    LOG_ERR("Video at " << path << " has no frames!!");
    return nullptr;
  }

  size_t actual_frame_mem_size = (size_t)test_frame.step[0] * height;
  size_t frame_size = actual_frame_mem_size;

  size_t aligned_cache_size = 64;
  size_t aligned_frame_size = (actual_frame_mem_size + aligned_cache_size - 1) & ~(aligned_cache_size - 1);
  size_t huge_page_size = 2 * 1024 * 1024;   // 2MB huge page: 512x fewer page faults than 4KB pages
  size_t total_size = (aligned_frame_size * count + huge_page_size - 1) & ~(huge_page_size - 1);   // aligned_alloc needs size to be a multiple of the alignment

  void* ptr_aligen = std::aligned_alloc(huge_page_size, total_size);
  uint8_t* raw_ptr_data = static_cast<uint8_t*>(ptr_aligen);

  // 2-4. check is it allocated properly
  if(!raw_ptr_data)
  {
    LOG_ERR("Memory allocation failed!");
    return nullptr;
  }
  LOG_DEBUG("Allocated (Aligned): " << total_size / (1024 * 1024) << " MB");

  // 2-5. ask the kernel to back the buffer with 2MB pages (THP is often set to "madvise" on Ubuntu)
  if(madvise(raw_ptr_data, total_size, MADV_HUGEPAGE) != 0)
  {
    LOG_DEBUG("madvise(MADV_HUGEPAGE) failed, using normal 4KB pages");
  }

  // 2-6. test_frame is already the first frame of the video, so save it as frame 0
  ASM_streaming_copy_avx2(raw_ptr_data, test_frame.data, frame_size);

  // 3. EXTRACTING VIDEO DATA
  size_t frame_idx = 1;
  int chunk_size = 64;
  std::vector<cv::Mat> chunk_frames(chunk_size);

  while (frame_idx < count)   // CAP_PROP_FRAME_COUNT can be wrong, so never write past the buffer
  {
    int read_count = 0;
    int to_read = (int)std::min<size_t>(chunk_size, count - frame_idx);

    for (int i = 0; i < to_read; ++i) 
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

        ASM_streaming_copy_avx2(dest, chunk_frames[i].data, frame_size);
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
  vd->count       = frame_idx;   // real number of frames read (can be less than CAP_PROP_FRAME_COUNT)
  vd->fps         = fps;
  vd->duration    = (fps > 0) ? (static_cast<double>(frame_idx) / fps) : duration;
  vd->frame_size  = frame_size;
  vd->aligned_frame_size  = aligned_frame_size;
  vd->total_alloc_size    = total_size;
  vd->data        = raw_ptr_data;

  return vd;
}

