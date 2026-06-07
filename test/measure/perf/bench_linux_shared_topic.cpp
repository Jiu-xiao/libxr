/**
 * @file bench_linux_shared_topic.cpp
 * @brief `LinuxSharedTopic` 基准聚合入口。 Aggregation entry for split `LinuxSharedTopic` benchmarks.
 */
#include "../../framework/test_bench_registry.hpp"

/**
 * @brief 辅助函数 `main`。 Helper function `main`.
 * @details 测试内容：按环境变量选择并执行拆分后的 benchmark 组。 Select and execute the split benchmark groups according to the environment selector.
 *          测试原理：把大 benchmark 收成多个子场景后，仍通过单一入口兼容原来的运行方式。 Preserve the original single-entry execution flow after splitting the oversized benchmark into multiple scenario files.
 */
int main()
{
  LibXR::PlatformInit();
  const char* bench_set = std::getenv("BENCH_SET");
  return RunBenchTestBinary(bench_set);
}
