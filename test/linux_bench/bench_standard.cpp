/**
 * @file bench_standard.cpp
 * @brief `LinuxSharedTopic` standard 基准聚合入口。 Aggregation entry for `LinuxSharedTopic` standard benchmarks.
 * @details 测试项目：
 *          1. 聚合不同 payload 大小与 payload touch 模式的 standard case。
 *          Test items:
 *          1. Aggregate standard cases across payload sizes and payload-touch modes.
 */
#include "linux_shared_topic_standard_bench_runner.hpp"

namespace LinuxSharedTopicBench
{
int RunStandardBenchmarksSmoke()
{
  return RunBenchCase<64, false>(256);
}

int RunStandardBenchmarks()
{
  int status = 0;
  status |= RunBenchCase<64, false>();
  status |= RunBenchCase<64, true>();
  status |= RunBenchCase<4096, false>();
  status |= RunBenchCase<4096, true>();
  status |= RunBenchCase<65536, false>();
  status |= RunBenchCase<65536, true>();
  status |= RunBenchCase<1048576, false>();
  status |= RunBenchCase<1048576, true>();
  return status;
}
}  // namespace LinuxSharedTopicBench
