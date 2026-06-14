/**
 * @file test_bench_sets.hpp
 * @brief benchmark 固定运行集合定义。 Fixed runtime-set definitions for benchmark binaries.
 * @details 作用：
 *          1. 强制 benchmark 只允许在 `full_os` 集合下运行。
 *          2. 保留 benchmark 内部自己的 `BENCH_SET` 分组选择。
 *          Purpose:
 *          1. Enforce that benchmarks run only under the `full_os` set.
 *          2. Preserve benchmark-local `BENCH_SET` group selection.
 */
#pragma once

#include "../measure/perf/linux_shared_topic_bench_common.hpp"
#include "test_runtime_set.hpp"

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
