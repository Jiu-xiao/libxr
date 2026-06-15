/**
 * @file linux_shared_topic_bench_stats_common.hpp
 * @brief `LinuxSharedTopic` 基准共用统计与 frame helper。 Shared stats and frame helpers for `LinuxSharedTopic` benchmarks.
 * @details 作用：
 *          1. 集中复用的时钟、统计结构和 payload frame 模板。
 *          2. 提供 payload 规模相关的计数、配置和延迟统计 helper。
 *          Purpose:
 *          1. Centralize reusable clock, statistics structures, and payload frame templates.
 *          2. Provide payload-dependent count/configuration and latency-statistics helpers.
 */
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "libxr.hpp"

namespace LinuxSharedTopicBench
{

using Clock = std::chrono::steady_clock;

inline uint64_t BenchScaleDivisor()
{
  const char* text = std::getenv("LIBXR_BENCH_SCALE_DIV");
  if (text == nullptr || text[0] == '\0')
  {
    return 1;
  }

  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, 10);
  if (end == text || (end != nullptr && *end != '\0') || parsed == 0)
  {
    return 1;
  }
  return static_cast<uint64_t>(parsed);
}

inline uint64_t ScaleBenchCount(uint64_t base_count, uint64_t minimum_count)
{
  const uint64_t divisor = BenchScaleDivisor();
  if (divisor <= 1)
  {
    return base_count;
  }

  const uint64_t scaled = base_count / divisor;
  return (scaled >= minimum_count) ? scaled : minimum_count;
}

inline uint64_t NowNs()
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
          .count());
}

struct BenchStats
{
  uint64_t count = 0;
  uint64_t sequence_errors = 0;
  uint64_t timeout_errors = 0;
  double min_us = 0.0;
  double avg_us = 0.0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
  double max_us = 0.0;
};

struct OverloadStats
{
  BenchStats latency = {};
  uint64_t subscriber_drop_num = 0;
  uint64_t sequence_gap = 0;
};

struct ModeSubConfig
{
  LibXR::LinuxSharedSubscriberMode mode = LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL;
  uint32_t delay_us = 0;
  const char* label = "";
};

struct ModeSubResult
{
  BenchStats latency = {};
  uint64_t recv_count = 0;
  uint64_t drop_count = 0;
  uint64_t first_seq = 0;
  uint64_t last_seq = 0;
  uint64_t timeout_errors = 0;
};

template <size_t PayloadBytes>
struct BenchFrame
{
  uint64_t seq = 0;
  uint64_t pub_ns = 0;
  uint32_t checksum = 0;
  uint32_t reserved = 0;
  std::array<uint8_t, PayloadBytes> payload = {};
};

template <size_t PayloadBytes>
uint32_t ComputeChecksum(const BenchFrame<PayloadBytes>& frame)
{
  return static_cast<uint32_t>((frame.seq * 1315423911ULL) ^
                               (frame.pub_ns * 2654435761ULL) ^ PayloadBytes);
}

template <size_t PayloadBytes>
uint64_t CountForPayload()
{
  if constexpr (PayloadBytes <= 64)
  {
    return ScaleBenchCount(100000, 64);
  }
  else if constexpr (PayloadBytes <= 4096)
  {
    return ScaleBenchCount(50000, 64);
  }
  else if constexpr (PayloadBytes <= 65536)
  {
    return ScaleBenchCount(5000, 32);
  }
  else
  {
    return ScaleBenchCount(256, 8);
  }
}

template <size_t PayloadBytes>
LibXR::LinuxSharedTopicConfig ConfigForPayload()
{
  LibXR::LinuxSharedTopicConfig config;
  config.subscriber_num = 1;
  if constexpr (PayloadBytes <= 64)
  {
    config.slot_num = 4096;
    config.queue_num = 4096;
  }
  else if constexpr (PayloadBytes <= 4096)
  {
    config.slot_num = 2048;
    config.queue_num = 2048;
  }
  else if constexpr (PayloadBytes <= 65536)
  {
    config.slot_num = 256;
    config.queue_num = 256;
  }
  else
  {
    config.slot_num = 64;
    config.queue_num = 64;
  }
  return config;
}

template <size_t PayloadBytes>
uint64_t LatencyCountForPayload()
{
  if constexpr (PayloadBytes <= 64)
  {
    return ScaleBenchCount(20000, 64);
  }
  else if constexpr (PayloadBytes <= 4096)
  {
    return ScaleBenchCount(10000, 64);
  }
  else if constexpr (PayloadBytes <= 65536)
  {
    return ScaleBenchCount(2000, 32);
  }
  else
  {
    return ScaleBenchCount(128, 8);
  }
}

template <size_t PayloadBytes>
BenchStats BuildStats(const std::vector<double>& lat_us, uint64_t sequence_errors,
                      uint64_t timeout_errors)
{
  BenchStats stats = {};
  stats.sequence_errors = sequence_errors;
  stats.timeout_errors = timeout_errors;
  if (lat_us.empty())
  {
    return stats;
  }

  stats.count = lat_us.size();
  stats.min_us = lat_us.front();
  stats.max_us = lat_us.front();
  double sum_us = 0.0;
  for (double value : lat_us)
  {
    stats.min_us = std::min(stats.min_us, value);
    stats.max_us = std::max(stats.max_us, value);
    sum_us += value;
  }
  stats.avg_us = sum_us / static_cast<double>(lat_us.size());

  std::vector<double> sorted = lat_us;
  std::sort(sorted.begin(), sorted.end());
  auto percentile = [&](double p) {
    const size_t index = static_cast<size_t>(
        std::floor((static_cast<double>(sorted.size() - 1U)) * p + 0.5));
    return sorted[index];
  };
  stats.p50_us = percentile(0.50);
  stats.p95_us = percentile(0.95);
  stats.p99_us = percentile(0.99);
  return stats;
}

}  // namespace LinuxSharedTopicBench
