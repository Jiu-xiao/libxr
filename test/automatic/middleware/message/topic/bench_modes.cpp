/**
 * @file bench_modes.cpp
 * @brief 共享 Topic 多订阅者基准 / Shared Topic multi-subscriber benchmark.
 *
 * 运行 LinuxSharedTopic 的不同订阅配置，分别记录接收数量、丢弃和延迟。
 * Run LinuxSharedTopic subscriber configurations and report each subscriber's receipts,
 * drops and latency.
 */

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "linux_shared_topic_bench_common.hpp"

namespace LinuxSharedTopicBench
{

template <size_t PayloadBytes, typename Subscriber, typename Data>
int RunModeSubscriberChild(const char* topic_name, const ModeSubConfig& subscriber_cfg,
                           uint64_t count, int done_fd, int stats_fd, int ready_fd)
{
  const int flags = fcntl(done_fd, F_GETFL, 0);
  (void)fcntl(done_fd, F_SETFL, flags | O_NONBLOCK);

  Subscriber subscriber(topic_name, subscriber_cfg.mode);
  if (!subscriber.Valid())
  {
    return 60;
  }

  std::vector<double> lat_us;
  lat_us.reserve(static_cast<size_t>(count));
  ModeSubResult result = {};
  const uint8_t ready = 1;
  if (!WriteAll(ready_fd, &ready, sizeof(ready)))
  {
    return 61;
  }

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
      if (subscriber_cfg.delay_us > 0)
      {
        usleep(subscriber_cfg.delay_us);
      }
      continue;
    }

    if (ans != LibXR::ErrorCode::TIMEOUT)
    {
      ++result.timeout_errors;
    }

    uint8_t done = 0;
    const ssize_t read_ans = read(done_fd, &done, sizeof(done));
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
  (void)WriteAll(stats_fd, &result, sizeof(result));
  return 0;
}

template <size_t PayloadBytes>
int RunModeCase(const char* case_label, const std::vector<ModeSubConfig>& subscribers,
                uint64_t count, uint32_t slot_num, uint32_t queue_num)
{
  // 为每个订阅者启动一个进程；全部就绪后才发布，结束时收集各自的接收和丢弃统计。
  // Start one process per subscriber; publish after all are ready, then collect receive
  // and drop counts.
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
  auto cleanup = MakeScopeExit(
      [&]()
      {
        for (auto& runtime : runtimes)
        {
          CloseFd(runtime.ready_pipe[0]);
          CloseFd(runtime.ready_pipe[1]);
          CloseFd(runtime.done_pipe[0]);
          CloseFd(runtime.done_pipe[1]);
          CloseFd(runtime.stats_pipe[0]);
          CloseFd(runtime.stats_pipe[1]);
          KillAndReapChild(runtime.pid);
        }
        (void)Topic::Remove(topic_name);
      });

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
      CloseFd(runtimes[i].ready_pipe[0]);
      CloseFd(runtimes[i].done_pipe[1]);
      CloseFd(runtimes[i].stats_pipe[0]);
      const int child_status = RunModeSubscriberChild<PayloadBytes, Subscriber, Data>(
          topic_name, subscribers[i], count, runtimes[i].done_pipe[0],
          runtimes[i].stats_pipe[1], runtimes[i].ready_pipe[1]);
      CloseFd(runtimes[i].done_pipe[0]);
      CloseFd(runtimes[i].stats_pipe[1]);
      CloseFd(runtimes[i].ready_pipe[1]);
      _exit(child_status);
    }

    runtimes[i].pid = child;
    CloseFd(runtimes[i].ready_pipe[1]);
    CloseFd(runtimes[i].done_pipe[0]);
    CloseFd(runtimes[i].stats_pipe[1]);
  }

  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    uint8_t ready = 0;
    if (!ReadAll(runtimes[i].ready_pipe[0], &ready, sizeof(ready)))
    {
      std::fprintf(stderr, "mode ready failed: %s[%zu]\n", case_label, i);
      return 1;
    }
    CloseFd(runtimes[i].ready_pipe[0]);
  }

  if (!WaitForSubscriberAttach(publisher, static_cast<uint32_t>(subscribers.size()),
                               case_label))
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
    std::memset(frame->payload.data(), static_cast<int>(seq & 0xFFU),
                frame->payload.size());
    frame->checksum = ComputeChecksum(*frame);

    if (publisher.Publish(data) != LibXR::ErrorCode::OK)
    {
      ++publish_fail;
      continue;
    }
    ++publish_ok;
  }
  const uint64_t end_ns = NowNs();

  for (auto& runtime : runtimes)
  {
    CloseFd(runtime.done_pipe[1]);
  }

  std::vector<ModeSubResult> results(subscribers.size());
  for (size_t i = 0; i < subscribers.size(); ++i)
  {
    const bool read_ok =
        ReadAll(runtimes[i].stats_pipe[0], &results[i], sizeof(results[i]));
    CloseFd(runtimes[i].stats_pipe[0]);
    int status = 0;
    while (waitpid(runtimes[i].pid, &status, 0) == -1 && errno == EINTR)
    {
    }
    runtimes[i].pid = -1;
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
                case_label, subscribers[i].label,
                static_cast<unsigned>(subscribers[i].mode), results[i].recv_count,
                results[i].drop_count, results[i].first_seq, results[i].last_seq,
                results[i].latency.avg_us, results[i].latency.p95_us);
  }
  std::fflush(stdout);

  return 0;
}

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

  status |=
      RunModeCase<65536>("broadcast_full_64k",
                         {{LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_a"},
                          {LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 0, "sub_b"}},
                         count_64k, 512, 32);
  status |= RunModeCase<65536>(
      "broadcast_drop_old_64k",
      {{LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50, "sub_slow"},
       {LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 0, "sub_fast"}},
      count_64k, 512, 8);
  status |=
      RunModeCase<65536>("balance_rr_64k",
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
