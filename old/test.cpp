extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <video_path>" << std::endl;
        return -1;
    }

    AVFormatContext* pFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) {
        std::cerr << "Could not open file" << std::endl;
        return -1;
    }

    std::cout << "Format: " << pFormatCtx->iformat->long_name << std::endl;
    std::cout << "Duration: " << pFormatCtx->duration / AV_TIME_BASE << " seconds" << std::endl;

    avformat_close_input(&pFormatCtx);
    return 0;
}
