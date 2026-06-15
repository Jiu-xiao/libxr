/**
 * @file linux_shared_topic_bench_io_common.hpp
 * @brief `LinuxSharedTopic` 基准共用 I/O 与 attach helper。 Shared I/O and attach helpers for `LinuxSharedTopic` benchmarks.
 * @details 作用：
 *          1. 提供 pipe 完整读写 helper。
 *          2. 提供等待 subscriber attach 的共用 helper。
 *          Purpose:
 *          1. Provide complete read/write helpers for pipes.
 *          2. Provide a shared helper that waits for subscriber attachment.
 */
#pragma once

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <sys/wait.h>
#include <unistd.h>
#include <type_traits>
#include <utility>

#include "linux_shared_topic_bench_stats_common.hpp"

namespace LinuxSharedTopicBench
{

template <typename Fn>
class ScopeExit
{
 public:
  explicit ScopeExit(Fn fn) : fn_(std::move(fn)) {}

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

  ScopeExit(ScopeExit&& other) noexcept : fn_(std::move(other.fn_)), active_(other.active_)
  {
    other.active_ = false;
  }

  ~ScopeExit()
  {
    if (active_)
    {
      fn_();
    }
  }

 private:
  Fn fn_;
  bool active_ = true;
};

template <typename Fn>
auto MakeScopeExit(Fn&& fn)
{
  return ScopeExit<std::decay_t<Fn>>(std::forward<Fn>(fn));
}

inline void CloseFd(int& fd)
{
  if (fd >= 0)
  {
    close(fd);
    fd = -1;
  }
}

inline void KillAndReapChild(pid_t& child)
{
  if (child <= 0)
  {
    return;
  }

  (void)::kill(child, SIGTERM);
  int child_status = 0;
  while (waitpid(child, &child_status, 0) == -1 && errno == EINTR)
  {
  }
  child = -1;
}

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

}  // namespace LinuxSharedTopicBench
