/**
 * @file bench_latency.cpp
 * @brief `LinuxSharedTopic` one-way latency 基准聚合入口。 Aggregation entry for `LinuxSharedTopic` one-way latency benchmarks.
 * @details 测试项目：
 *          1. 聚合不同 payload 大小与 payload touch 模式的 one-way latency case。
 *          Test items:
 *          1. Aggregate one-way latency cases across payload sizes and payload-touch modes.
 */
#include "linux_shared_topic_latency_bench_runner.hpp"

namespace LinuxSharedTopicBench
{
int RunLatencyBenchmarks()
{
  int status = 0;
  status |= RunLatencyCase<64, false>();
  status |= RunLatencyCase<64, true>();
  status |= RunLatencyCase<4096, false>();
  status |= RunLatencyCase<4096, true>();
  status |= RunLatencyCase<65536, false>();
  status |= RunLatencyCase<65536, true>();
  status |= RunLatencyCase<1048576, false>();
  status |= RunLatencyCase<1048576, true>();
  return status;
}
}  // namespace LinuxSharedTopicBench
