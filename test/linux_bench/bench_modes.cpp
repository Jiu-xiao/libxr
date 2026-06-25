/**
 * @file bench_modes.cpp
 * @brief `LinuxSharedTopic` subscriber mode 基准聚合入口。 Aggregation entry for `LinuxSharedTopic` subscriber-mode benchmarks.
 * @details 测试项目：
 *          1. 聚合 64 KiB 与 1 MiB payload 的 subscriber mode 对比 case。
 *          Test items:
 *          1. Aggregate subscriber-mode comparison cases for 64 KiB and 1 MiB payloads.
 */
#include "linux_shared_topic_mode_bench_runner.hpp"

namespace LinuxSharedTopicBench
{
int RunModeBenchmarksSmoke()
{
  return RunModeCase<65536>(
      "broadcast_full_64k_smoke",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
      256, 512, 32);
}

int RunModeBenchmarks()
{
  int status = 0;
  const uint64_t count_64k = ScaleBenchCount(4000, 32);
  const uint64_t count_1m = ScaleBenchCount(256, 8);

  status |= RunModeCase<65536>(
      "broadcast_full_64k",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
      count_64k, 512, 32);
  status |= RunModeCase<65536>(
      "broadcast_drop_old_64k",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50, "sub_slow"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 0, "sub_fast"}},
      count_64k, 512, 8);
  status |= RunModeCase<65536>(
      "balance_rr_64k",
      {{LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_a"},
       {LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_b"}},
      count_64k, 512, 32);
  status |= RunModeCase<1048576>(
      "broadcast_full_1m",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
      count_1m, 64, 8);
  status |= RunModeCase<1048576>(
      "broadcast_drop_old_1m",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50, "sub_slow"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 0, "sub_fast"}},
      count_1m, 64, 4);
  status |= RunModeCase<1048576>(
      "balance_rr_1m",
      {{LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_a"},
       {LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_b"}},
      count_1m, 64, 8);
  return status;
}

}  // namespace LinuxSharedTopicBench
