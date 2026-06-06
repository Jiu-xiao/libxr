/**
 * @file bench_modes.cpp
 * @brief `LinuxSharedTopic` subscriber mode 对比子基准。 Split benchmark unit for subscriber-mode comparison cases.
 */
#include "linux_shared_topic_bench_common.hpp"

namespace LinuxSharedTopicBench
{
/**
 * @brief 执行辅助函数 `RunModeCase`。 Execution helper function `RunModeCase`.
 * @details 测试内容：执行一个 subscriber mode 对比子场景。 Execute one subscriber-mode comparison sub-case.
 *          测试原理：把 broadcast / balance 等模式组合统一放进一个可复用执行函数里。 Place broadcast/balance combinations behind one reusable execution helper.
 */
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
      std::fprintf(stderr, "pipe failed for %s[%zu]: %s\n", case_label, i, std::strerror(errno));
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

      const int flags = fcntl(runtimes[i].done_pipe[0], F_GETFL, 0);
      (void)fcntl(runtimes[i].done_pipe[0], F_SETFL, flags | O_NONBLOCK);

      Subscriber subscriber(topic_name, subscribers[i].mode);
      if (!subscriber.Valid())
      {
        _exit(60);
      }

      std::vector<double> lat_us;
      lat_us.reserve(static_cast<size_t>(count));

      ModeSubResult result = {};
      const uint8_t ready = 1;
      if (!WriteAll(runtimes[i].ready_pipe[1], &ready, sizeof(ready)))
      {
        _exit(61);
      }
      close(runtimes[i].ready_pipe[1]);
      while (true)
      {
        Data data;
        const LibXR::ErrorCode ans = subscriber.Wait(data, 100);
        if (ans == LibXR::ErrorCode::OK)
        {
          const auto* frame = data.GetData();
          if (frame != nullptr)
          {
            if (result.recv_count == 0)
            {
              result.first_seq = frame->seq;
            }
            result.last_seq = frame->seq;
            ++result.recv_count;
            lat_us.push_back(static_cast<double>(NowNs() - frame->pub_ns) / 1000.0);
          }
          if (subscribers[i].delay_us > 0)
          {
            usleep(subscribers[i].delay_us);
          }
          continue;
        }

        if (ans != LibXR::ErrorCode::TIMEOUT)
        {
          ++result.timeout_errors;
        }

        uint8_t done = 0;
        const ssize_t read_ans = read(runtimes[i].done_pipe[0], &done, sizeof(done));
        if (read_ans == 0)
        {
          break;
        }
        if (read_ans < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          break;
        }
      }

      result.latency = BuildStats<PayloadBytes>(lat_us, 0, result.timeout_errors);
      result.drop_count = subscriber.GetDropNum();
      (void)WriteAll(runtimes[i].stats_pipe[1], &result, sizeof(result));
      close(runtimes[i].done_pipe[0]);
      close(runtimes[i].stats_pipe[1]);
      _exit(0);
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

/**
 * @brief 执行辅助函数 `RunModeBenchmarks`。 Execution helper function `RunModeBenchmarks`.
 * @details 测试内容：批量执行 subscriber mode 对比基准集合。 Execute the batch of subscriber-mode comparison benchmark cases.
 *          测试原理：把不同 payload 与模式组合集中到一个入口里统一对比。 Group payload/mode combinations behind one entry for consistent comparison.
 */
int RunModeBenchmarks()
{
  int status = 0;
  status |= RunModeCase<65536>(
      "broadcast_full_64k",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
      4000, 512, 32);
  status |= RunModeCase<65536>(
      "broadcast_drop_old_64k",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50, "sub_slow"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 0, "sub_fast"}},
      4000, 512, 8);
  status |= RunModeCase<65536>(
      "balance_rr_64k",
      {{LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_a"},
       {LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_b"}},
      4000, 512, 32);
  status |= RunModeCase<1048576>(
      "broadcast_full_1m",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
      256, 64, 8);
  status |= RunModeCase<1048576>(
      "broadcast_drop_old_1m",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50, "sub_slow"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 0, "sub_fast"}},
      256, 64, 4);
  status |= RunModeCase<1048576>(
      "balance_rr_1m",
      {{LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_a"},
       {LibXR::LinuxSharedSubscriberMode::BALANCE_RR, 0, "worker_b"}},
      256, 64, 8);
  return status;
}

}  // namespace LinuxSharedTopicBench
