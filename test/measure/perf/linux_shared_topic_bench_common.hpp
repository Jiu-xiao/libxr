/**
 * @file linux_shared_topic_bench_common.hpp
 * @brief `LinuxSharedTopic` 基准聚合 helper 入口。 Aggregation helper entry for `LinuxSharedTopic` benchmarks.
 * @details 作用：
 *          1. 聚合统计/frame helper 与 I/O/attach helper。
 *          2. 对外只保留 benchmark 组运行入口声明。
 *          Purpose:
 *          1. Aggregate stats/frame helpers with I/O/attach helpers.
 *          2. Expose only the benchmark-group runner declarations.
 */
#pragma once

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "linux_shared_topic_bench_io_common.hpp"

namespace LinuxSharedTopicBench
{
int RunStandardBenchmarks();
int RunLatencyBenchmarks();
int RunOverloadBenchmarks();
int RunModeBenchmarks();
}  // namespace LinuxSharedTopicBench
