/**
 * @file main.cpp
 * @brief base/runtime 测试主执行器聚合入口。 Aggregation entry for the base/runtime main
 * test runner.
 * @details 职责：
 *          1. 安装把断言失败转换成进程失败的 fatal 测试回调。
 *          2. 调用拆分后的测试分组注册表。
 *          Responsibilities:
 *          1. Register the fatal test callback that converts assertion failures into
 * process failure.
 *          2. Invoke the split test-group registry.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "test_main_sets.hpp"

extern const char kRwIsrReadBlockScenario[];
extern const char kRwIsrWriteBlockScenario[];
int RunRwIsrReadBlockScenario();
int RunRwIsrWriteBlockScenario();

/**
 * @brief 辅助函数 `main`。 Helper function `main`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

int main(int argc, char** argv)
{
  if (argc == 2 && std::strcmp(argv[1], kRwIsrReadBlockScenario) == 0)
  {
    return RunRwIsrReadBlockScenario();
  }
  if (argc == 2 && std::strcmp(argv[1], kRwIsrWriteBlockScenario) == 0)
  {
    return RunRwIsrWriteBlockScenario();
  }

  LibXR::PlatformInit();

  auto err_cb = LibXR::Assert::FatalCallback::Create(
      [](bool in_isr, void* arg, const char* file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        std::fprintf(stderr, "Error: Union test failed at step [%s].\r\n", test_name);
        std::fflush(stderr);
        std::abort();
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  if (argc == 2 && std::strcmp(argv[1], "--rw-only") == 0)
  {
    test_rw();
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--cdc-only") == 0)
  {
    test_cdc_uart_tx();
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--uart-dma-only") == 0)
  {
    test_uart_dma_tx_model();
    return 0;
  }

  const int status = RunMainTestBinary();
  exit(status);
  return status;
}
