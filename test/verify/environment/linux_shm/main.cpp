/**
 * @file main.cpp
 * @brief Linux shared-memory 环境验证执行器。 Runner for Linux shared-memory environment verification.
 *
 * 职责 / Responsibilities:
 * 1. 安装验证二进制的 fatal assertion 回调。 Install the fatal assertion callback for the verification binary.
 * 2. 执行 `LinuxSharedTopic` 的环境验证入口。 Execute the `LinuxSharedTopic` environment verification entrypoint.
 */
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
