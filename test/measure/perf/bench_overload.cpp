/**
 * @file bench_overload.cpp
 * @brief `LinuxSharedTopic` overload 基准聚合入口。 Aggregation entry for `LinuxSharedTopic` overload benchmarks.
 * @details 测试项目：
 *          1. 聚合不同 payload 与广播策略的 overload case。
 *          Test items:
 *          1. Aggregate overload cases across payload sizes and broadcast policies.
 */
#include "linux_shared_topic_overload_bench_runner.hpp"

namespace LinuxSharedTopicBench
{
int RunOverloadBenchmarks()
{
  int status = 0;
  status |= RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
  status |= RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
  status |= RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
  status |= RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
  return status;
}
}  // namespace LinuxSharedTopicBench
