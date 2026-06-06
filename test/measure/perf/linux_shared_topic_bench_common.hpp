/**
 * @file linux_shared_topic_bench_common.hpp
 * @brief `LinuxSharedTopic` 基准共用类型与 helper。 Shared types and helpers for `LinuxSharedTopic` benchmarks.
 *
 * 作用 / Purpose:
 * 1. 集中性能基准复用的 frame、统计结构和 I/O helper。
 *    Centralize reusable frames, statistics structures, and I/O helpers used by the benchmarks.
 * 2. 让不同 benchmark case 文件只保留各自的测量场景。
 *    Keep each benchmark case file focused on its own measurement scenario.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"

namespace LinuxSharedTopicBench
{

using Clock = std::chrono::steady_clock;

/**
 * @brief 辅助函数 `NowNs`。 Helper function `NowNs`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
/**
 * @brief 辅助函数 `ComputeChecksum`。 Helper function `ComputeChecksum`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint32_t ComputeChecksum(const BenchFrame<PayloadBytes>& frame)
{
  return static_cast<uint32_t>((frame.seq * 1315423911ULL) ^
                               (frame.pub_ns * 2654435761ULL) ^ PayloadBytes);
}

template <size_t PayloadBytes>
/**
 * @brief 辅助函数 `CountForPayload`。 Helper function `CountForPayload`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t CountForPayload()
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  if constexpr (PayloadBytes <= 64)
  {
    return 100000;
  }
  else if constexpr (PayloadBytes <= 4096)
  {
    return 50000;
  }
  else if constexpr (PayloadBytes <= 65536)
  {
    return 5000;
  }
  else
  {
    return 256;
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

/**
 * @brief 辅助函数 `WriteAll`。 Helper function `WriteAll`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline bool WriteAll(int fd, const void* buffer, size_t size)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  size_t written_total = 0;
  while (written_total < size)
  {
    const ssize_t written = write(fd, bytes + written_total, size - written_total);
    if (written > 0)
    {
      written_total += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR)
    {
      continue;
    }
    return false;
  }
  return true;
}

/**
 * @brief 辅助函数 `ReadAll`。 Helper function `ReadAll`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline bool ReadAll(int fd, void* buffer, size_t size)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  auto* bytes = static_cast<uint8_t*>(buffer);
  size_t read_total = 0;
  while (read_total < size)
  {
    const ssize_t read_size = read(fd, bytes + read_total, size - read_total);
    if (read_size > 0)
    {
      read_total += static_cast<size_t>(read_size);
      continue;
    }
    if (read_size < 0 && errno == EINTR)
    {
      continue;
    }
    return false;
  }
  return true;
}

template <typename TopicType>
/**
 * @brief 辅助函数 `WaitForSubscriberAttach`。 Helper function `WaitForSubscriberAttach`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
bool WaitForSubscriberAttach(TopicType& topic, uint32_t expected_num, const char* case_label)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  for (int retry = 0; retry < 500 && topic.GetSubscriberNum() < expected_num; ++retry)
  {
    usleep(1000);
  }
  if (topic.GetSubscriberNum() < expected_num)
  {
    std::fprintf(stderr, "%s subscriber attach timeout: expected=%u actual=%u\n", case_label,
                 expected_num, topic.GetSubscriberNum());
    return false;
  }
  return true;
}

template <size_t PayloadBytes>
/**
 * @brief 辅助函数 `LatencyCountForPayload`。 Helper function `LatencyCountForPayload`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t LatencyCountForPayload()
{
  if constexpr (PayloadBytes <= 64)
  {
    return 20000;
  }
  else if constexpr (PayloadBytes <= 4096)
  {
    return 10000;
  }
  else if constexpr (PayloadBytes <= 65536)
  {
    return 2000;
  }
  else
  {
    return 128;
  }
}

int RunStandardBenchmarks();
int RunLatencyBenchmarks();
int RunOverloadBenchmarks();
int RunModeBenchmarks();
}  // namespace LinuxSharedTopicBench
