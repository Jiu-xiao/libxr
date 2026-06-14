/**
 * @file test_bench_sets.hpp
 * @brief benchmark 入口集合定义。 Benchmark entry-set definition.
 * @details 作用：
 *          1. 保留 benchmark 内部自己的 `BENCH_SET` 分组选择。
 *          2. 提供主 Linux 测试 runner 可直接调用的 bench helper。
 *          Purpose:
 *          1. Preserve benchmark-local `BENCH_SET` group selection.
 *          2. Provide a bench helper directly callable from the main Linux test runner.
 */
#pragma once

#include "../measure/perf/linux_shared_topic_bench_common.hpp"
inline int RunBenchLinuxSharedTopicSet(const char* selector)
{
  int status = 0;
  const bool run_standard =
      (selector == nullptr) || (std::strcmp(selector, "standard") == 0);
  const bool run_latency =
      (selector == nullptr) || (std::strcmp(selector, "latency") == 0);
  const bool run_overload =
      (selector == nullptr) || (std::strcmp(selector, "overload") == 0);
  const bool run_modes = (selector == nullptr) || (std::strcmp(selector, "modes") == 0);

  if (run_standard)
  {
    status |= LinuxSharedTopicBench::RunStandardBenchmarks();
  }
  if (run_latency)
  {
    status |= LinuxSharedTopicBench::RunLatencyBenchmarks();
  }
  if (run_overload)
  {
    status |= LinuxSharedTopicBench::RunOverloadBenchmarks();
  }
  if (run_modes)
  {
    status |= LinuxSharedTopicBench::RunModeBenchmarks();
  }
  return status;
}

inline int RunBenchLinuxSharedTopicAllSet()
{
  return RunBenchLinuxSharedTopicSet(nullptr);
}
