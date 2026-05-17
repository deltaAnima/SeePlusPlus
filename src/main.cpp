#include <iostream>
#include <logger.hpp>

int main(int argc, char* argv[])
{
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--verbose" || arg == "-v")   verbose = true;
    else if (arg == "--help" || arg == "-h") {
      std::cout <<
        "usage: vn-discord-rpc [options]\n\n"
        "options:\n"
        "  -v, --verbose  enable debug-level logging\n"
        "  -h, --help     show this message\n\n";
      return 0;
    }
  }

  if (verbose) {
    Logger::get().setLevel(LogLevel::DEBUG);
  }
}

