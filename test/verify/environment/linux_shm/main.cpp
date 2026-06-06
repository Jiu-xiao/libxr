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

/**
 * @brief 辅助函数 `main`。 Helper function `main`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
