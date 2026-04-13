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

#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

uint64_t NowNs()
{
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

bool WriteAll(int fd, const void* buffer, size_t size)
{
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

bool ReadAll(int fd, void* buffer, size_t size)
{
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
bool WaitForSubscriberAttach(TopicType& topic, uint32_t expected_num, const char* case_label)
{
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

template <size_t PayloadBytes, bool TouchPayload>
int RunBenchCase()
{
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

      const double latency_us =
          static_cast<double>(NowNs() - frame->pub_ns) / 1000.0;
      lat_us.push_back(latency_us);
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
    else
    {
      if constexpr (PayloadBytes > 0)
      {
        frame->payload[0] = static_cast<uint8_t>(seq & 0xFFU);
        frame->payload[PayloadBytes - 1U] = static_cast<uint8_t>((seq >> 8U) & 0xFFU);
      }
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
  const double publish_avg_us = static_cast<double>(end_ns - start_ns) / 1000.0 /
                                static_cast<double>(count);

  std::printf(
      "[BENCH] shared_standard mode=%s payload=%zuB count=%" PRIu64
      " publish_avg=%.3f us throughput=%.0f msg/s bandwidth=%.2f MiB/s "
      "create_retry=%" PRIu64 " publish_retry=%" PRIu64
      " latency_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us "
      "seq_err=%" PRIu64 " timeout_err=%" PRIu64 "\n",
      TouchPayload ? "full-touch" : "transport", sizeof(BenchFrame<PayloadBytes>), count,
      publish_avg_us, msg_per_s, mib_per_s,
      create_retries, publish_retries, stats.avg_us, stats.p50_us, stats.p95_us, stats.p99_us,
      stats.max_us, stats.sequence_errors, stats.timeout_errors);
  std::fflush(stdout);

  (void)Topic::Remove(topic_name);
  return 0;
}

// Measure one-way delivery with at most one outstanding message so queueing backlog
// and startup burst do not pollute the latency distribution.
template <size_t PayloadBytes, bool TouchPayload>
int RunLatencyCase()
{
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  const uint64_t count = LatencyCountForPayload<PayloadBytes>();

  LibXR::LinuxSharedTopicConfig config = {};
  config.slot_num = 4;
  config.subscriber_num = 1;
  config.queue_num = 4;

  char topic_name[96] = {};
  std::snprintf(topic_name, sizeof(topic_name), "linux_shared_latency_%zu_%d", PayloadBytes,
                static_cast<int>(getpid()));
  (void)Topic::Remove(topic_name);

  int ready_pipe[2] = {-1, -1};
  int ack_pipe[2] = {-1, -1};
  if (pipe(ready_pipe) != 0 || pipe(ack_pipe) != 0)
  {
    std::fprintf(stderr, "latency pipe failed: %s\n", std::strerror(errno));
    return 1;
  }

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "latency publisher open failed for payload=%zu\n", PayloadBytes);
    return 1;
  }

  pid_t child = fork();
  if (child < 0)
  {
    std::fprintf(stderr, "latency fork failed: %s\n", std::strerror(errno));
    return 1;
  }

  if (child == 0)
  {
    close(ready_pipe[0]);
    close(ack_pipe[0]);

    Subscriber subscriber(topic_name);
    if (!subscriber.Valid())
    {
      _exit(70);
    }

    const uint8_t ready = 1;
    if (!WriteAll(ready_pipe[1], &ready, sizeof(ready)))
    {
      _exit(71);
    }
    close(ready_pipe[1]);

    uint64_t expected_seq = 1;
    for (uint64_t i = 0; i < count; ++i)
    {
      Data data;
      if (subscriber.Wait(data, 5000) != LibXR::ErrorCode::OK)
      {
        _exit(72);
      }

      const auto* frame = data.GetData();
      if (frame == nullptr || frame->seq != expected_seq ||
          frame->checksum != ComputeChecksum(*frame))
      {
        _exit(73);
      }
      expected_seq = frame->seq + 1U;

      const uint64_t latency_ns = NowNs() - frame->pub_ns;
      if (!WriteAll(ack_pipe[1], &latency_ns, sizeof(latency_ns)))
      {
        _exit(74);
      }
    }

    close(ack_pipe[1]);
    _exit(0);
  }

  close(ready_pipe[1]);
  close(ack_pipe[1]);

  uint8_t ready = 0;
  if (!ReadAll(ready_pipe[0], &ready, sizeof(ready)) ||
      !WaitForSubscriberAttach(publisher, 1, "latency"))
  {
    close(ready_pipe[0]);
    close(ack_pipe[0]);
    return 1;
  }
  close(ready_pipe[0]);

  std::vector<double> lat_us;
  lat_us.reserve(static_cast<size_t>(count));
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

    uint64_t latency_ns = 0;
    if (!ReadAll(ack_pipe[0], &latency_ns, sizeof(latency_ns)))
    {
      close(ack_pipe[0]);
      waitpid(child, nullptr, 0);
      std::fprintf(stderr, "latency child ack failed for payload=%zu\n", PayloadBytes);
      return 1;
    }
    lat_us.push_back(static_cast<double>(latency_ns) / 1000.0);
  }
  const uint64_t end_ns = NowNs();

  close(ack_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
  {
    std::fprintf(stderr, "latency child failed for payload=%zu\n", PayloadBytes);
    return 1;
  }

  const BenchStats stats = BuildStats<PayloadBytes>(lat_us, 0, 0);
  const double total_s = static_cast<double>(end_ns - start_ns) / 1e9;
  const double exchange_rate = static_cast<double>(count) / total_s;
  std::printf(
      "[BENCH] shared_latency mode=%s payload=%zuB count=%" PRIu64
      " exchange_rate=%.0f msg/s create_retry=%" PRIu64 " publish_retry=%" PRIu64
      " one_way_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us\n",
      TouchPayload ? "full-touch" : "transport", sizeof(BenchFrame<PayloadBytes>), count,
      exchange_rate, create_retries, publish_retries, stats.avg_us, stats.p50_us, stats.p95_us,
      stats.p99_us, stats.max_us);
  std::fflush(stdout);

  (void)Topic::Remove(topic_name);
  return 0;
}

template <size_t PayloadBytes>
int RunOverloadCase(LibXR::LinuxSharedSubscriberMode subscriber_mode,
                    uint32_t subscriber_delay_us)
{
  using Topic = LibXR::LinuxSharedTopic<BenchFrame<PayloadBytes>>;
  using Data = typename Topic::Data;
  using Subscriber = typename Topic::SyncSubscriber;

  const uint64_t count = (PayloadBytes >= 1048576U) ? 256U : 4000U;

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
  if (pipe(done_pipe) != 0 || pipe(stats_pipe) != 0 || pipe(ready_pipe) != 0)
  {
    std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
    return 1;
  }

  Topic publisher(topic_name, config);
  if (!publisher.Valid())
  {
    std::fprintf(stderr, "overload publisher open failed for payload=%zu\n", PayloadBytes);
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
    close(done_pipe[1]);
    close(stats_pipe[0]);
    close(ready_pipe[0]);

    const int flags = fcntl(done_pipe[0], F_GETFL, 0);
    (void)fcntl(done_pipe[0], F_SETFL, flags | O_NONBLOCK);

    Subscriber subscriber(topic_name, subscriber_mode);
    if (!subscriber.Valid())
    {
      _exit(30);
    }

    std::vector<double> lat_us;
    lat_us.reserve(static_cast<size_t>(count));
    uint64_t expected_seq = 1;
    uint64_t sequence_gap = 0;
    uint64_t timeout_errors = 0;
    const uint8_t ready = 1;
    if (!WriteAll(ready_pipe[1], &ready, sizeof(ready)))
    {
      _exit(31);
    }
    close(ready_pipe[1]);

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
      const ssize_t read_ans = read(done_pipe[0], &done, sizeof(done));
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
    (void)WriteAll(stats_pipe[1], &stats, sizeof(stats));
    close(done_pipe[0]);
    close(stats_pipe[1]);
    _exit(0);
  }

  close(done_pipe[0]);
  close(stats_pipe[1]);
  close(ready_pipe[1]);

  uint8_t ready = 0;
  if (!ReadAll(ready_pipe[0], &ready, sizeof(ready)) ||
      !WaitForSubscriberAttach(publisher, 1, "overload"))
  {
    close(ready_pipe[0]);
    return 1;
  }
  close(ready_pipe[0]);

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

  close(done_pipe[1]);

  OverloadStats stats = {};
  const bool read_ok = ReadAll(stats_pipe[0], &stats, sizeof(stats));
  close(stats_pipe[0]);

  int status = 0;
  waitpid(child, &status, 0);
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
      " publish_fail=%" PRIu64 " recv=%" PRIu64 " sub_drop=%" PRIu64
      " seq_gap=%" PRIu64 " lat_avg=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us max=%.3f us\n",
      subscriber_mode == LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL ? "FULL" : "DROP_OLD",
      sizeof(BenchFrame<PayloadBytes>), count, subscriber_delay_us, attempt_rate, ok_rate,
      create_fail, publish_fail, stats.latency.count, stats.subscriber_drop_num,
      stats.sequence_gap, stats.latency.avg_us, stats.latency.p50_us, stats.latency.p95_us,
      stats.latency.p99_us, stats.latency.max_us);
  std::fflush(stdout);

  (void)Topic::Remove(topic_name);
  return 0;
}

template <size_t PayloadBytes>
int RunModeCase(const char* case_label, const std::vector<ModeSubConfig>& subscribers,
                uint64_t count, uint32_t slot_num, uint32_t queue_num)
{
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

}  // namespace

int main()
{
  LibXR::PlatformInit();

  int status = 0;
  const char* bench_set = std::getenv("BENCH_SET");
  const bool run_standard = (bench_set == nullptr) || (std::strcmp(bench_set, "standard") == 0);
  const bool run_latency = (bench_set == nullptr) || (std::strcmp(bench_set, "latency") == 0);
  const bool run_overload = (bench_set == nullptr) || (std::strcmp(bench_set, "overload") == 0);
  const bool run_modes = (bench_set == nullptr) || (std::strcmp(bench_set, "modes") == 0);

  if (run_standard)
  {
    status |= RunBenchCase<64, false>();
    status |= RunBenchCase<64, true>();
    status |= RunBenchCase<4096, false>();
    status |= RunBenchCase<4096, true>();
    status |= RunBenchCase<65536, false>();
    status |= RunBenchCase<65536, true>();
    status |= RunBenchCase<1048576, false>();
    status |= RunBenchCase<1048576, true>();
  }

  if (run_latency)
  {
    status |= RunLatencyCase<64, false>();
    status |= RunLatencyCase<64, true>();
    status |= RunLatencyCase<4096, false>();
    status |= RunLatencyCase<4096, true>();
    status |= RunLatencyCase<65536, false>();
    status |= RunLatencyCase<65536, true>();
    status |= RunLatencyCase<1048576, false>();
    status |= RunLatencyCase<1048576, true>();
  }

  if (run_overload)
  {
    status |= RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
    status |=
        RunOverloadCase<65536>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
    status |= RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_FULL, 50);
    status |=
        RunOverloadCase<1048576>(LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD, 50);
  }

  if (run_modes)
  {
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
  }

  return status;
}
