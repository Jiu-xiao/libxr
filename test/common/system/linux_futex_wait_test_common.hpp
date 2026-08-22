#pragma once

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>

namespace LibXRTest
{

inline constexpr uint32_t LINUX_FUTEX_OBSERVATION_TIMEOUT_MS = 10000U;

inline pid_t CurrentLinuxThreadId() { return static_cast<pid_t>(syscall(SYS_gettid)); }

enum class LinuxFutexWaitMode : uint8_t
{
  NONE,
  TIMED,
  UNTIMED,
};

inline LinuxFutexWaitMode GetLinuxFutexWaitMode(pid_t thread_id)
{
  std::ifstream syscall_state("/proc/self/task/" + std::to_string(thread_id) +
                              "/syscall");
  long syscall_number = -1;
  uintptr_t futex_address = 0U;
  uintptr_t futex_operation = 0U;
  uintptr_t expected_value = 0U;
  uintptr_t timeout_address = 0U;
  syscall_state >> std::dec >> syscall_number >> std::hex >> futex_address >>
      futex_operation >> expected_value >> timeout_address;

  if (syscall_state.fail() || syscall_number != SYS_futex ||
      (futex_operation & FUTEX_CMD_MASK) != FUTEX_WAIT)
  {
    return LinuxFutexWaitMode::NONE;
  }

  return timeout_address == 0U ? LinuxFutexWaitMode::UNTIMED : LinuxFutexWaitMode::TIMED;
}

inline bool IsThreadBlockedInFutexWait(pid_t thread_id)
{
  return GetLinuxFutexWaitMode(thread_id) != LinuxFutexWaitMode::NONE;
}

inline bool WaitForLinuxFutexWait(
    pid_t thread_id, uint32_t timeout_ms = LINUX_FUTEX_OBSERVATION_TIMEOUT_MS)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!IsThreadBlockedInFutexWait(thread_id))
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

inline bool WaitForLinuxFutexWait(
    const std::atomic<pid_t>& thread_id,
    uint32_t timeout_ms = LINUX_FUTEX_OBSERVATION_TIMEOUT_MS)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true)
  {
    const pid_t observed = thread_id.load(std::memory_order_acquire);
    if (observed > 0 && IsThreadBlockedInFutexWait(observed))
    {
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
}

inline bool WaitForLinuxFutexWaitMode(
    const std::atomic<pid_t>& thread_id, LinuxFutexWaitMode expected,
    uint32_t timeout_ms = LINUX_FUTEX_OBSERVATION_TIMEOUT_MS)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (true)
  {
    const pid_t observed = thread_id.load(std::memory_order_acquire);
    if (observed > 0 && GetLinuxFutexWaitMode(observed) == expected)
    {
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
}

}  // namespace LibXRTest
