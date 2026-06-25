#include "semaphore.hpp"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>

#include "libxr_def.hpp"
#include "monotonic_time.hpp"

using namespace LibXR;

namespace
{

int FutexWait(std::atomic<uint32_t>* word, uint32_t expected, const struct timespec* timeout)
{
  return static_cast<int>(syscall(SYS_futex, reinterpret_cast<uint32_t*>(word), FUTEX_WAIT,
                                  expected, timeout, nullptr, 0));
}

int FutexWake(std::atomic<uint32_t>* word, int count)
{
  return static_cast<int>(syscall(SYS_futex, reinterpret_cast<uint32_t*>(word), FUTEX_WAKE,
                                  count, nullptr, nullptr, 0));
}

}  // namespace

Semaphore::Semaphore(uint32_t init_count) : semaphore_handle_(new libxr_linux_futex_semaphore)
{
  semaphore_handle_->count.store(init_count, std::memory_order_release);
}

Semaphore::~Semaphore()
{
  delete semaphore_handle_;
}

void Semaphore::Post()
{
  const uint32_t prev = semaphore_handle_->count.fetch_add(1, std::memory_order_release);
  if (prev == 0)
  {
    (void)FutexWake(&semaphore_handle_->count, 1);
  }
}

ErrorCode Semaphore::Wait(uint32_t timeout)
{
  const uint64_t deadline_ms =
      (timeout == UINT32_MAX) ? UINT64_MAX : (MonotonicTime::NowMilliseconds() + timeout);

  while (true)
  {
    uint32_t current = semaphore_handle_->count.load(std::memory_order_acquire);

    while (current > 0)
    {
      if (semaphore_handle_->count.compare_exchange_weak(current, current - 1U,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }

    timespec ts = {};
    timespec* ts_ptr = nullptr;
    if (timeout != UINT32_MAX)
    {
      const uint32_t remaining_ms = MonotonicTime::RemainingMilliseconds(deadline_ms);
      if (remaining_ms == 0)
      {
        return ErrorCode::TIMEOUT;
      }
      ts = MonotonicTime::RelativeFromMilliseconds(remaining_ms);
      ts_ptr = &ts;
    }

    const int ans = FutexWait(&semaphore_handle_->count, 0, ts_ptr);
    if (ans == 0 || errno == EAGAIN || errno == EINTR)
    {
      continue;
    }

    if (errno == ETIMEDOUT)
    {
      return ErrorCode::TIMEOUT;
    }

    return ErrorCode::FAILED;
  }
}

void Semaphore::PostFromCallback(bool in_isr)
{
  UNUSED(in_isr);
  Post();
}

size_t Semaphore::Value()
{
  return semaphore_handle_->count.load(std::memory_order_acquire);
}
