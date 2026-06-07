/**
 * @file linux_shared_topic_mode_child_common.hpp
 * @brief `LinuxSharedTopic` subscriber mode 子进程 helper。 Shared subscriber-child helper for `LinuxSharedTopic` mode benchmarks.
 * @details 作用：
 *          1. 封装 mode benchmark 中 subscriber 子进程的接收与统计逻辑。
 *          2. 减小 mode runner 主执行函数的体积。
 *          Purpose:
 *          1. Encapsulate receive/statistics logic for subscriber children in mode benchmarks.
 *          2. Reduce the size of the main mode runner function.
 */
#pragma once

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

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

}  // namespace LinuxSharedTopicBench
