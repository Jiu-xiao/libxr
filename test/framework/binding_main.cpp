/**
 * @file binding_main.cpp
 * @brief binding 平面测试主执行器。 Main runner for binding-plane tests.
 *
 * 职责 / Responsibilities:
 * 1. 安装 binding 测试的 fatal assertion 回调。 Install the fatal assertion callback for binding tests.
 * 2. 按确定顺序执行 binding 平面测试入口。 Run the binding-specific test entrypoints in a deterministic order.
 */
#include "test_binding_registry.hpp"

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

        XR_LOG_ERROR("Error: binding test failed.\r\n");
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);
  return RunBindingTestBinary();
}
