#include "libxr.hpp"
#include "timer.hpp"

static_assert(configTICK_RATE_HZ == 1000, "configTICK_RATE_HZ must be 1000");

#if defined(configUSE_TIMERS) && (configUSE_TIMERS + 0)
#warning \
    "It is recommended to set configUSE_TIMERS to 0 to reduce resource usage unless software timers are strictly required."
#endif

// NOLINTNEXTLINE
extern "C" __attribute__((weak)) void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                                                    char *pcTaskName)
{
  static volatile const char *task_name = pcTaskName;
  UNUSED(task_name);
  UNUSED(xTask);
  ASSERT(false);
}

// NOLINTNEXTLINE
extern "C" __attribute__((weak)) BaseType_t xTaskCatchUpTicks(TickType_t xTicksToCatchUp)
{
  vTaskStepTick(xTicksToCatchUp);
  return pdPASS;
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

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }

void operator delete[](void *ptr, std::size_t size) noexcept
{
  ::operator delete(ptr, size);
}

void *operator new(std::size_t size, std::align_val_t align)
{
  std::size_t a = static_cast<std::size_t>(align);
  std::size_t space = size + a + sizeof(void *);
  void *raw = pvPortMalloc(space);
  if (raw == nullptr)
  {
    ASSERT(false);
    return raw;  // NOLINT
  }

  uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *);
  uintptr_t aligned_addr = (raw_addr + a - 1) & ~(a - 1);
  void *aligned_ptr = reinterpret_cast<void *>(aligned_addr);  // NOLINT
  reinterpret_cast<void **>(aligned_ptr)[-1] = raw;

  return aligned_ptr;
}

void operator delete(void *ptr, std::align_val_t) noexcept
{
  if (ptr)
  {
    vPortFree((static_cast<void **>(ptr))[-1]);
  }
}

void operator delete(void *ptr, std::size_t, std::align_val_t align) noexcept
{
  operator delete(ptr, align);
}

void *operator new[](std::size_t size, std::align_val_t align)
{
  return ::operator new(size, align);
}

void operator delete[](void *ptr, std::align_val_t align) noexcept
{
  ::operator delete(ptr, align);
}

void operator delete[](void *ptr, std::size_t, std::align_val_t align) noexcept
{
  ::operator delete(ptr, align);
}

#endif
