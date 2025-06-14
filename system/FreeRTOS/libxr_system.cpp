#include "libxr.hpp"
#include "timer.hpp"

static_assert(configTICK_RATE_HZ == 1000, "configTICK_RATE_HZ must be 1000");

extern "C" __attribute__((weak)) void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                                                    char *pcTaskName)
{
  static volatile const char *task_name = pcTaskName;
  UNUSED(task_name);
  UNUSED(xTask);
  ASSERT(false);
}

void LibXR::PlatformInit(uint32_t timer_pri, uint32_t timer_stack_depth)
{
  if (Timebase::timebase == nullptr)
  {
    /* You should initialize Timebase first */
    ASSERT(false);
  }

  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;

  int64_t time_need_to_catch_up = static_cast<int64_t>(Timebase::GetMilliseconds()) -
                                  static_cast<int64_t>(xTaskGetTickCount());

  if (time_need_to_catch_up > 0)
  {
    xTaskCatchUpTicks(time_need_to_catch_up);
  }
}

#ifndef ESP_PLATFORM
void *operator new(std::size_t size)
{
  if (size == 0)
  {
    return pvPortMalloc(size);
  }

#ifdef LIBXR_DEBUG_BUILD
  static volatile uint32_t free_size = 0;
  UNUSED(free_size);
  free_size = xPortGetFreeHeapSize();
#endif

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
#endif
