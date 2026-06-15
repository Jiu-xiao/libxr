/**
 * @file linux_shared_topic_overload_bench_runner.hpp
 * @brief `LinuxSharedTopic` overload 基准执行 helper。 Shared execution helper for `LinuxSharedTopic` overload benchmarks.
 * @details 作用：
 *          1. 封装 overload benchmark 的 parent 侧发布与统计流程。
 *          2. 让 `bench_overload.cpp` 只保留策略/载荷组合。
 *          Purpose:
 *          1. Encapsulate the publisher-side flow and statistics of overload benchmarks.
 *          2. Keep `bench_overload.cpp` focused on policy/payload combinations only.
 */
#pragma once

#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "linux_shared_topic_overload_child_common.hpp"

namespace LinuxSharedTopicBench
{

template <size_t PayloadBytes>
int RunOverloadCase(LibXR::LinuxSharedSubscriberMode subscriber_mode,
                    uint32_t subscriber_delay_us)
{
  // 基准内容：执行当前子场景或 case。
  // Benchmark coverage: execute the current benchmark sub-case.
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  const uint64_t count =
      (PayloadBytes >= 1048576U) ? ScaleBenchCount(256U, 8U) : ScaleBenchCount(4000U, 32U);

  LibXR::LinuxSharedTopicConfig config = {};
  config.subscriber_num = 1;
  if constexpr (PayloadBytes >= 1048576U)
  {
    config.slot_num = 64;
    config.queue_num = 4;
  }
  else
  {
    config.slot_num = 512;
    config.queue_num = 8;
  }

  char topic_name[96] = {};
  std::snprintf(topic_name, sizeof(topic_name), "linux_shared_overload_%zu_%u_%d", PayloadBytes,
                static_cast<unsigned>(subscriber_mode), static_cast<int>(getpid()));
  (void)Topic::Remove(topic_name);

  int done_pipe[2] = {-1, -1};
  int stats_pipe[2] = {-1, -1};
  int ready_pipe[2] = {-1, -1};
  pid_t child = -1;
  auto close_fd = [](int& fd)
  {
    if (fd >= 0)
    {
      close(fd);
      fd = -1;
    }
  };
  auto cleanup = [&]()
  {
    close_fd(done_pipe[0]);
    close_fd(done_pipe[1]);
    close_fd(stats_pipe[0]);
    close_fd(stats_pipe[1]);
    close_fd(ready_pipe[0]);
    close_fd(ready_pipe[1]);
    if (child > 0)
    {
      (void)::kill(child, SIGTERM);
      int child_status = 0;
      while (waitpid(child, &child_status, 0) == -1 && errno == EINTR)
      {
      }
      child = -1;
    }
    (void)Topic::Remove(topic_name);
  };

  if (pipe(done_pipe) != 0 || pipe(stats_pipe) != 0 || pipe(ready_pipe) != 0)
  {
    std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
    cleanup();
    return 1;
  }

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "overload publisher open failed for payload=%zu\n", PayloadBytes);
    cleanup();
    return 1;
  }

  child = fork();
  if (child < 0)
  {
    std::fprintf(stderr, "fork failed: %s\n", std::strerror(errno));
    cleanup();
    return 1;
  }

  if (child == 0)
  {
    close_fd(done_pipe[1]);
    close_fd(stats_pipe[0]);
    close_fd(ready_pipe[0]);
    const int child_status = RunOverloadSubscriberChild<PayloadBytes, Subscriber, Data>(
        topic_name, subscriber_mode, subscriber_delay_us, count, done_pipe[0], stats_pipe[1],
        ready_pipe[1]);
    close_fd(done_pipe[0]);
    close_fd(stats_pipe[1]);
    close_fd(ready_pipe[1]);
    _exit(child_status);
  }

  close_fd(done_pipe[0]);
  close_fd(stats_pipe[1]);
  close_fd(ready_pipe[1]);

  uint8_t ready = 0;
  if (!ReadAll(ready_pipe[0], &ready, sizeof(ready)) ||
      !WaitForSubscriberAttach(publisher, 1, "overload"))
  {
    cleanup();
    return 1;
  }
  close_fd(ready_pipe[0]);

  uint64_t create_fail = 0;
  uint64_t publish_fail = 0;
  uint64_t publish_ok = 0;

  const uint64_t start_ns = NowNs();
  for (uint64_t seq = 1; seq <= count; ++seq)
  {
    Data data;
    if (publisher.CreateData(data) != LibXR::ErrorCode::OK)
    {
      ++create_fail;
      continue;
    }

    auto* frame = data.GetData();
    frame->seq = seq;
    frame->pub_ns = NowNs();
    std::memset(frame->payload.data(), static_cast<int>(seq & 0xFFU), frame->payload.size());
    frame->checksum = ComputeChecksum(*frame);

    if (publisher.Publish(data) != LibXR::ErrorCode::OK)
    {
      ++publish_fail;
      continue;
    }
    ++publish_ok;
  }
  const uint64_t end_ns = NowNs();

  close_fd(done_pipe[1]);

  OverloadStats stats = {};
  const bool read_ok = ReadAll(stats_pipe[0], &stats, sizeof(stats));
  close_fd(stats_pipe[0]);

  int status = 0;
  while (waitpid(child, &status, 0) == -1 && errno == EINTR)
  {
  }
  child = -1;
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    std::fprintf(stderr, "overload child failed for payload=%zu\n", PayloadBytes);
    cleanup();
    return 1;
  }

  const double total_s = static_cast<double>(end_ns - start_ns) / 1e9;
  const double attempt_rate = static_cast<double>(count) / total_s;
  const double ok_rate = static_cast<double>(publish_ok) / total_s;
  std::printf(
      "[BENCH] shared_overload policy=%s payload=%zuB count=%" PRIu64
      " delay=%u us attempt_rate=%.0f msg/s ok_rate=%.0f msg/s create_fail=%" PRIu64
      " publish_fail=%" PRIu64 " recv=%" PRIu64 " sub_drop=%" PRIu64
      " seq_gap=%" PRIu64 " lat_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us\n",
      subscriber_mode == LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL ? "FULL" : "DROP_OLD",
      sizeof(BenchFrame<PayloadBytes>), count, subscriber_delay_us, attempt_rate, ok_rate,
      create_fail, publish_fail, stats.latency.count, stats.subscriber_drop_num,
      stats.sequence_gap, stats.latency.avg_us, stats.latency.p50_us, stats.latency.p95_us,
      stats.latency.p99_us, stats.latency.max_us);
  std::fflush(stdout);

  cleanup();
  return 0;
}

}  // namespace LinuxSharedTopicBench
