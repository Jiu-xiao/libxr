/**
 * @file bench_overload.cpp
 * @brief 共享 Topic 慢订阅者基准 / Shared Topic slow-subscriber benchmark.
 *
 * 让 LinuxSharedTopic 订阅者延迟处理，记录发布失败、丢弃和延迟，没有固定性能门槛。
 * Delay the LinuxSharedTopic subscriber and report failures, drops and latency without a
 * fixed performance threshold.
 */

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "linux_shared_topic_bench_common.hpp"

namespace LinuxSharedTopicBench
{

template <size_t PayloadBytes, typename Subscriber, typename Data>
int RunOverloadSubscriberChild(const char* topic_name,
                               LibXR::LinuxSharedSubscriberMode subscriber_mode,
                               uint32_t subscriber_delay_us, uint64_t count, int done_fd,
                               int stats_fd, int ready_fd)
{
  const int flags = fcntl(done_fd, F_GETFL, 0);
  (void)fcntl(done_fd, F_SETFL, flags | O_NONBLOCK);

  Subscriber subscriber(topic_name, subscriber_mode);
  if (!subscriber.Valid())
  {
    return 30;
  }

  std::vector<double> lat_us;
  lat_us.reserve(static_cast<size_t>(count));
  uint64_t expected_seq = 1;
  uint64_t sequence_gap = 0;
  uint64_t timeout_errors = 0;
  const uint8_t ready = 1;
  if (!WriteAll(ready_fd, &ready, sizeof(ready)))
  {
    return 31;
  }

  while (true)
  {
    Data data;
    LibXR::ErrorCode ans = subscriber.Wait(data, 100);
    if (ans == LibXR::ErrorCode::OK)
    {
      const auto* frame = data.GetData();
      if (frame == nullptr)
      {
        ++timeout_errors;
        continue;
      }

      if (frame->seq > expected_seq)
      {
        sequence_gap += (frame->seq - expected_seq);
      }
      expected_seq = frame->seq + 1U;

      lat_us.push_back(static_cast<double>(NowNs() - frame->pub_ns) / 1000.0);
      if (subscriber_delay_us > 0)
      {
        usleep(subscriber_delay_us);
      }
      continue;
    }

    if (ans != LibXR::ErrorCode::TIMEOUT)
    {
      ++timeout_errors;
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

  OverloadStats stats = {};
  stats.latency = BuildStats<PayloadBytes>(lat_us, 0, timeout_errors);
  stats.sequence_gap = sequence_gap;
  stats.subscriber_drop_num = subscriber.GetDropNum();
  (void)WriteAll(stats_fd, &stats, sizeof(stats));
  return 0;
}

template <size_t PayloadBytes>
int RunOverloadCase(LibXR::LinuxSharedSubscriberMode subscriber_mode,
                    uint32_t subscriber_delay_us, uint64_t count_override = 0)
{
  // 让订阅者故意处理得更慢，记录队列压力下的发布失败和接收情况；没有固定性能门槛。
  // Slow the subscriber to record publish failures and receipts under pressure; no fixed
  // performance threshold.
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  const uint64_t count = (count_override != 0)
                             ? count_override
                             : ((PayloadBytes >= 1048576U) ? ScaleBenchCount(256U, 8U)
                                                           : ScaleBenchCount(4000U, 32U));

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
  std::snprintf(topic_name, sizeof(topic_name), "linux_shared_overload_%zu_%u_%d",
                PayloadBytes, static_cast<unsigned>(subscriber_mode),
                static_cast<int>(getpid()));
  (void)Topic::Remove(topic_name);

  int done_pipe[2] = {-1, -1};
  int stats_pipe[2] = {-1, -1};
  int ready_pipe[2] = {-1, -1};
  pid_t child = -1;
  auto cleanup = MakeScopeExit(
      [&]()
      {
        CloseFd(done_pipe[0]);
        CloseFd(done_pipe[1]);
        CloseFd(stats_pipe[0]);
        CloseFd(stats_pipe[1]);
        CloseFd(ready_pipe[0]);
        CloseFd(ready_pipe[1]);
        KillAndReapChild(child);
        (void)Topic::Remove(topic_name);
      });

  if (pipe(done_pipe) != 0 || pipe(stats_pipe) != 0 || pipe(ready_pipe) != 0)
  {
    std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
    return 1;
  }

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "overload publisher open failed for payload=%zu\n",
                 PayloadBytes);
    return 1;
  }

  child = fork();
  if (child < 0)
  {
    std::fprintf(stderr, "fork failed: %s\n", std::strerror(errno));
    return 1;
  }

  if (child == 0)
  {
    CloseFd(done_pipe[1]);
    CloseFd(stats_pipe[0]);
    CloseFd(ready_pipe[0]);
    const int child_status = RunOverloadSubscriberChild<PayloadBytes, Subscriber, Data>(
        topic_name, subscriber_mode, subscriber_delay_us, count, done_pipe[0],
        stats_pipe[1], ready_pipe[1]);
    CloseFd(done_pipe[0]);
    CloseFd(stats_pipe[1]);
    CloseFd(ready_pipe[1]);
    _exit(child_status);
  }

  CloseFd(done_pipe[0]);
  CloseFd(stats_pipe[1]);
  CloseFd(ready_pipe[1]);

  uint8_t ready = 0;
  if (!ReadAll(ready_pipe[0], &ready, sizeof(ready)) ||
      !WaitForSubscriberAttach(publisher, 1, "overload"))
  {
    return 1;
  }
  CloseFd(ready_pipe[0]);

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

  CloseFd(done_pipe[1]);

  OverloadStats stats = {};
  const bool read_ok = ReadAll(stats_pipe[0], &stats, sizeof(stats));
  CloseFd(stats_pipe[0]);

  int status = 0;
  while (waitpid(child, &status, 0) == -1 && errno == EINTR)
  {
  }
  child = -1;
  if (!read_ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    std::fprintf(stderr, "overload child failed for payload=%zu\n", PayloadBytes);
    return 1;
  }

  const double total_s = static_cast<double>(end_ns - start_ns) / 1e9;
  const double attempt_rate = static_cast<double>(count) / total_s;
  const double ok_rate = static_cast<double>(publish_ok) / total_s;
  std::printf(
      "[BENCH] shared_overload policy=%s payload=%zuB count=%" PRIu64
      " delay=%u us attempt_rate=%.0f msg/s ok_rate=%.0f msg/s create_fail=%" PRIu64
      " publish_fail=%" PRIu64 " recv=%" PRIu64 " sub_drop=%" PRIu64 " seq_gap=%" PRIu64
      " lat_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us\n",
      subscriber_mode == LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL ? "FULL"
                                                                          : "DROP_OLD",
      sizeof(BenchFrame<PayloadBytes>), count, subscriber_delay_us, attempt_rate, ok_rate,
      create_fail, publish_fail, stats.latency.count, stats.subscriber_drop_num,
      stats.sequence_gap, stats.latency.avg_us, stats.latency.p50_us,
      stats.latency.p95_us, stats.latency.p99_us, stats.latency.max_us);
  std::fflush(stdout);

  return 0;
}

int RunOverloadBenchmarksSmoke()
{
  return RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50,
                                256);
}

int RunOverloadBenchmarks()
{
  int status = 0;
  status |= RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
  status |=
      RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
  status |=
      RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
  status |=
      RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
  return status;
}
}  // namespace LinuxSharedTopicBench
