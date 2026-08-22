#include "semaphore.hpp"

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "timer.hpp"

using namespace LibXR;

namespace
{

bool TryTake(uint32_t* value)
{
  uint32_t observed = __atomic_load_n(value, __ATOMIC_ACQUIRE);
  while (observed != 0U)
  {
    if (__atomic_compare_exchange_n(value, &observed, observed - 1U, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return true;
    }
  }
  return false;
}

}  // namespace

Semaphore::Semaphore(uint32_t init_count) : semaphore_handle_(init_count) {}

Semaphore::~Semaphore() {}

void Semaphore::Post()
{
  (void)__atomic_fetch_add(&semaphore_handle_, 1U, __ATOMIC_RELEASE);
}

ErrorCode Semaphore::Wait(uint32_t timeout)
{
  if (TryTake(&semaphore_handle_))
  {
    return ErrorCode::OK;
  }
  else if (timeout == 0)
  {
    return ErrorCode::TIMEOUT;
  }

  if (timeout == UINT32_MAX)
  {
    while (!TryTake(&semaphore_handle_))
    {
      Timer::RefreshTimerInIdle();
    }
    return ErrorCode::OK;
  }

  uint32_t now = Timebase::GetMilliseconds();

  while (uint32_t(Timebase::GetMilliseconds()) - now < timeout)
  {
    if (TryTake(&semaphore_handle_))
    {
      return ErrorCode::OK;
    }
    Timer::RefreshTimerInIdle();
  }
  return ErrorCode::TIMEOUT;
}

void Semaphore::PostFromCallback(bool in_isr)
{
  UNUSED(in_isr);
  Post();
}

size_t Semaphore::Value()
{
  return __atomic_load_n(&semaphore_handle_, __ATOMIC_ACQUIRE);
}
