/**
 * @file linux_shared_topic_mode_bench_runner.hpp
 * @brief `LinuxSharedTopic` subscriber mode 基准执行 helper。 Shared execution helper for `LinuxSharedTopic` subscriber-mode benchmarks.
 * @details 作用：
 *          1. 封装 mode benchmark 的 parent 侧发布与结果聚合流程。
 *          2. 让 `bench_modes.cpp` 只保留模式组合。
 *          Purpose:
 *          1. Encapsulate the publisher-side flow and result aggregation of mode benchmarks.
 *          2. Keep `bench_modes.cpp` focused on mode combinations only.
 */
#pragma once

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "linux_shared_topic_mode_child_common.hpp"

namespace LinuxSharedTopicBench
{

template <size_t PayloadBytes>
int RunModeCase(const char* case_label, const std::vector<ModeSubConfig>& subscribers,
                uint64_t count, uint32_t slot_num, uint32_t queue_num)
{
  // 基准内容：执行当前子场景或 case。
  // Benchmark coverage: execute the current benchmark sub-case.
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  LibXR::LinuxSharedTopicConfig config = {};
  config.subscriber_num = static_cast<uint32_t>(subscribers.size());
  config.slot_num = slot_num;
  config.queue_num = queue_num;

  char topic_name[96] = {};
  std::snprintf(topic_name, sizeof(topic_name), "linux_shared_modes_%zu_%d", PayloadBytes,
                static_cast<int>(getpid()));
  (void)Topic::Remove(topic_name);

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "mode publisher open failed: %s payload=%zu\n", case_label,
                 PayloadBytes);
    return 1;
  }

  struct ChildRuntime
  {
    pid_t pid = -1;
    int ready_pipe[2] = {-1, -1};
    int done_pipe[2] = {-1, -1};
    int stats_pipe[2] = {-1, -1};
  };

  std::vector<ChildRuntime> runtimes(subscribers.size());
  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    if (pipe(runtimes[i].ready_pipe) != 0 || pipe(runtimes[i].done_pipe) != 0 ||
        pipe(runtimes[i].stats_pipe) != 0)
    {
      std::fprintf(stderr, "pipe failed for %s[%zu]: %s\n", case_label, i,
                   std::strerror(errno));
      return 1;
    }

    pid_t child = fork();
    if (child < 0)
    {
      std::fprintf(stderr, "fork failed for %s[%zu]: %s\n", case_label, i,
                   std::strerror(errno));
      return 1;
    }

    if (child == 0)
    {
      close(runtimes[i].ready_pipe[0]);
      close(runtimes[i].done_pipe[1]);
      close(runtimes[i].stats_pipe[0]);
      const int child_status = RunModeSubscriberChild<PayloadBytes, Subscriber, Data>(
          topic_name, subscribers[i], count, runtimes[i].done_pipe[0], runtimes[i].stats_pipe[1],
          runtimes[i].ready_pipe[1]);
      close(runtimes[i].done_pipe[0]);
      close(runtimes[i].stats_pipe[1]);
      close(runtimes[i].ready_pipe[1]);
      _exit(child_status);
    }

    runtimes[i].pid = child;
    close(runtimes[i].ready_pipe[1]);
    close(runtimes[i].done_pipe[0]);
    close(runtimes[i].stats_pipe[1]);
  }

  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    uint8_t ready = 0;
    if (!ReadAll(runtimes[i].ready_pipe[0], &ready, sizeof(ready)))
    {
      close(runtimes[i].ready_pipe[0]);
      std::fprintf(stderr, "mode ready failed: %s[%zu]\n", case_label, i);
      return 1;
    }
    close(runtimes[i].ready_pipe[0]);
  }

  if (!WaitForSubscriberAttach(publisher, static_cast<uint32_t>(subscribers.size()), case_label))
  {
    return 1;
  }

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

  for (const auto& runtime : runtimes)
  {
    close(runtime.done_pipe[1]);
  }

  std::vector<ModeSubResult> results(subscribers.size());
  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    const bool read_ok = ReadAll(runtimes[i].stats_pipe[0], &results[i], sizeof(results[i]));
    close(runtimes[i].stats_pipe[0]);
    int status = 0;
    waitpid(runtimes[i].pid, &status, 0);
    if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
      std::fprintf(stderr, "mode child failed: %s[%zu]\n", case_label, i);
      return 1;
    }
  }

  const double total_s = static_cast<double>(end_ns - start_ns) / 1e9;
  const double ok_rate = static_cast<double>(publish_ok) / total_s;
  std::printf("[BENCH] shared_mode_summary case=%s payload=%zuB count=%" PRIu64
              " ok_rate=%.0f msg/s create_fail=%" PRIu64 " publish_fail=%" PRIu64 "\n",
              case_label, sizeof(BenchFrame<PayloadBytes>), count, ok_rate, create_fail,
              publish_fail);

  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    std::printf("[BENCH] shared_mode_subscriber case=%s sub=%s mode=%u recv=%" PRIu64
                " drop=%" PRIu64 " first=%" PRIu64 " last=%" PRIu64
                " lat_avg=%.3f us p95=%.3f us\n",
                case_label, subscribers[i].label, static_cast<unsigned>(subscribers[i].mode),
                results[i].recv_count, results[i].drop_count, results[i].first_seq,
                results[i].last_seq, results[i].latency.avg_us, results[i].latency.p95_us);
  }
  std::fflush(stdout);

  (void)Topic::Remove(topic_name);
  return 0;
}

}  // namespace LinuxSharedTopicBench
