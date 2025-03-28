#include "libxr.hpp"
#include "timer.hpp"

void LibXR::PlatformInit(uint32_t timer_pri, uint32_t timer_stack_depth)
{
  if (Timebase::timebase == nullptr)
  {
    /* You should initialize Timebase first */
    ASSERT(false);
  }

  LibXR::Timer::priority = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth = timer_stack_depth;

  uint32_t time_need_to_catch_up = Timebase::GetMilliseconds() - xTaskGetTickCount();

  if (time_need_to_catch_up > 0)
  {
    xTaskCatchUpTicks(time_need_to_catch_up);
  }
}

void *operator new(std::size_t size)
{
  if (size == 0)
  {
    return nullptr; // NOLINT
  }
  auto ans = pvPortMalloc(size);
  ASSERT(ans != nullptr);
  return ans;
}

void operator delete(void *ptr) noexcept { vPortFree(ptr); }
void operator delete(void *ptr, std::size_t size) noexcept
{
  UNUSED(size);
  vPortFree(ptr);
}
