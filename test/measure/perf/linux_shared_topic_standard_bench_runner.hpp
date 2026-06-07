/**
 * @file linux_shared_topic_standard_bench_runner.hpp
 * @brief `LinuxSharedTopic` standard 基准执行 helper。 Shared execution helper for `LinuxSharedTopic` standard benchmarks.
 * @details 作用：
 *          1. 封装 standard throughput/latency 子场景的通用执行流程。
 *          2. 让 `bench_standard.cpp` 只保留 payload case 组合。
 *          Purpose:
 *          1. Encapsulate the common execution flow for standard throughput/latency cases.
 *          2. Keep `bench_standard.cpp` focused on payload-case composition only.
 */
#pragma once

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "linux_shared_topic_bench_common.hpp"

namespace LinuxSharedTopicBench
{

template <size_t PayloadBytes, bool TouchPayload>
int RunBenchCase()
{
  // 基准内容：执行当前子场景或 case。
  // Benchmark coverage: execute the current benchmark sub-case.
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  const uint64_t count = CountForPayload<PayloadBytes>();
  const LibXR::LinuxSharedTopicConfig config = ConfigForPayload<PayloadBytes>();

  char topic_name[96] = {};
  std::snprintf(topic_name, sizeof(topic_name), "linux_shared_bench_%zu_%d", PayloadBytes,
                static_cast<int>(getpid()));
  (void)Topic::Remove(topic_name);

  int stats_pipe[2] = {-1, -1};
  int ready_pipe[2] = {-1, -1};
  if (pipe(stats_pipe) != 0 || pipe(ready_pipe) != 0)
  {
    std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
    return 1;
  }

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "publisher open failed for payload=%zu\n", PayloadBytes);
    return 1;
  }

  pid_t child = fork();
  if (child < 0)
  {
    std::fprintf(stderr, "fork failed: %s\n", std::strerror(errno));
    return 1;
  }

  if (child == 0)
  {
    close(stats_pipe[0]);
    close(ready_pipe[0]);

    Subscriber subscriber(topic_name);
    if (!subscriber.Valid())
    {
      _exit(2);
    }

    std::vector<double> lat_us;
    lat_us.reserve(static_cast<size_t>(count));
    uint64_t sequence_errors = 0;
    uint64_t timeout_errors = 0;
    uint64_t expected_seq = 1;
    const uint8_t ready = 1;
    if (!WriteAll(ready_pipe[1], &ready, sizeof(ready)))
    {
      _exit(3);
    }
    close(ready_pipe[1]);

    for (uint64_t i = 0; i < count; ++i)
    {
      Data data;
      LibXR::ErrorCode ans = subscriber.Wait(data, 5000);
      if (ans != LibXR::ErrorCode::OK)
      {
        ++timeout_errors;
        continue;
      }

      const auto* frame = data.GetData();
      if (frame == nullptr)
      {
        ++timeout_errors;
        continue;
      }

      if (frame->seq != expected_seq || frame->checksum != ComputeChecksum(*frame))
      {
        ++sequence_errors;
      }
      expected_seq = frame->seq + 1U;
      lat_us.push_back(static_cast<double>(NowNs() - frame->pub_ns) / 1000.0);
    }

    BenchStats stats = BuildStats<PayloadBytes>(lat_us, sequence_errors, timeout_errors);
    (void)WriteAll(stats_pipe[1], &stats, sizeof(stats));
    close(stats_pipe[1]);
    _exit(0);
  }

  close(stats_pipe[1]);
  close(ready_pipe[1]);

  uint8_t ready = 0;
  if (!ReadAll(ready_pipe[0], &ready, sizeof(ready)) ||
      !WaitForSubscriberAttach(publisher, 1, "standard"))
  {
    close(ready_pipe[0]);
    return 1;
  }
  close(ready_pipe[0]);

  uint64_t create_retries = 0;
  uint64_t publish_retries = 0;

  const uint64_t start_ns = NowNs();
  for (uint64_t seq = 1; seq <= count; ++seq)
  {
    Data data;
    while (publisher.CreateData(data) != LibXR::ErrorCode::OK)
    {
      ++create_retries;
      sched_yield();
    }

    auto* frame = data.GetData();
    frame->seq = seq;
    frame->pub_ns = NowNs();
    if constexpr (TouchPayload)
    {
      std::memset(frame->payload.data(), static_cast<int>(seq & 0xFFU), frame->payload.size());
    }
    else if constexpr (PayloadBytes > 0)
    {
      frame->payload[0] = static_cast<uint8_t>(seq & 0xFFU);
      frame->payload[PayloadBytes - 1U] = static_cast<uint8_t>((seq >> 8U) & 0xFFU);
    }
    frame->checksum = ComputeChecksum(*frame);

    while (publisher.Publish(data) != LibXR::ErrorCode::OK)
    {
      ++publish_retries;
      sched_yield();
    }
  }
  const uint64_t end_ns = NowNs();

  BenchStats stats = {};
  const bool read_ok = ReadAll(stats_pipe[0], &stats, sizeof(stats));
  close(stats_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    std::fprintf(stderr, "bench child failed for payload=%zu\n", PayloadBytes);
    return 1;
  }

  const double total_s = static_cast<double>(end_ns - start_ns) / 1e9;
  const double msg_per_s = static_cast<double>(count) / total_s;
  const double mib_per_s =
      (static_cast<double>(count) * static_cast<double>(sizeof(BenchFrame<PayloadBytes>)) /
       (1024.0 * 1024.0)) /
      total_s;
  const double publish_avg_us =
      static_cast<double>(end_ns - start_ns) / 1000.0 / static_cast<double>(count);

  std::printf(
      "[BENCH] shared_standard mode=%s payload=%zuB count=%" PRIu64
      " publish_avg=%.3f us throughput=%.0f msg/s bandwidth=%.2f MiB/s "
      "create_retry=%" PRIu64 " publish_retry=%" PRIu64
      " latency_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us "
      "seq_err=%" PRIu64 " timeout_err=%" PRIu64 "\n",
      TouchPayload ? "full-touch" : "transport", sizeof(BenchFrame<PayloadBytes>), count,
      publish_avg_us, msg_per_s, mib_per_s, create_retries, publish_retries, stats.avg_us,
      stats.p50_us, stats.p95_us, stats.p99_us, stats.max_us, stats.sequence_errors,
      stats.timeout_errors);
  std::fflush(stdout);

  (void)Topic::Remove(topic_name);
  return 0;
}

}  // namespace LinuxSharedTopicBench
