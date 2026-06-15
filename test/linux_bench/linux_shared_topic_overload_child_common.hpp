/**
 * @file linux_shared_topic_overload_child_common.hpp
 * @brief `LinuxSharedTopic` overload subscriber 子进程 helper。 Shared subscriber-child helper for `LinuxSharedTopic` overload benchmarks.
 * @details 作用：
 *          1. 封装 overload 场景下 subscriber 侧的接收、延迟和 drop 统计。
 *          2. 减小 overload runner 主执行函数的体积。
 *          Purpose:
 *          1. Encapsulate receive/latency/drop collection on the subscriber side of overload cases.
 *          2. Reduce the size of the main overload runner function.
 */
#pragma once

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

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

}  // namespace LinuxSharedTopicBench
