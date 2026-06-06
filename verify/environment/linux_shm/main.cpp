#include "test_verify.hpp"

#include <cstdlib>

#include "libxr.hpp"
#include "logger.hpp"

int main()
{
  LibXR::PlatformInit();

  auto err_cb = LibXR::Assert::FatalCallback::Create(
      [](bool in_isr, void* arg, const char* file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        XR_LOG_ERROR("Error: linux_shm_topic verification failed.\r\n");
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  test_linux_shm_topic();
  return 0;
}
