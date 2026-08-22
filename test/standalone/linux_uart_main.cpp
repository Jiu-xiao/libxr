#include <signal.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>

#include "libxr.hpp"

int RunLinuxUartScenario(const char* name);

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    return 2;
  }

  alarm(15U);
  LibXR::PlatformInit();
  auto fatal_callback = LibXR::Assert::FatalCallback::Create(
      [](bool, void*, const char* file, uint32_t line)
      {
        std::fprintf(stderr, "LinuxUART scenario fatal at %s:%u\n", file, line);
        std::fflush(stderr);
        _exit(200);
      },
      static_cast<void*>(nullptr));
  LibXR::Assert::RegisterFatalErrorCallback(fatal_callback);

  const int result = RunLinuxUartScenario(argv[1]);
  _exit(result);
}
