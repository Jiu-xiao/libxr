/**
 * @file main.cpp
 * @brief base/runtime 测试主执行器聚合入口。 Aggregation entry for the base/runtime main test runner.
 * @details 职责：
 *          1. 安装把断言失败转换成进程失败的 fatal 测试回调。
 *          2. 调用拆分后的测试分组注册表。
 *          Responsibilities:
 *          1. Register the fatal test callback that converts assertion failures into process failure.
 *          2. Invoke the split test-group registry.
 */
#include "test_group_registry.hpp"

#include <cmath>

/**
 * @brief 辅助函数 `main`。 Helper function `main`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

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

        XR_LOG_ERROR("Error: Union test failed at step [%s].\r\n", test_name);
        // NOLINTNEXTLINE
        *(volatile long long*)(nullptr) = 0;
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  run_libxr_tests();

  exit(0);

  return 0;
}
