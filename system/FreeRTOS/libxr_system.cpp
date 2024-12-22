#include "libxr_system.hpp"

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"

void LibXR::PlatformInit() {
  if (Timebase::timebase == NULL) {
    /* You should initialize Timebase first */
    ASSERT(false);
  }

  uint32_t time_need_to_catch_up =
      Timebase::GetMilliseconds() - xTaskGetTickCount();

  if (time_need_to_catch_up > 0) {
    xTaskCatchUpTicks(time_need_to_catch_up);
  }
}

void *operator new(std::size_t size) { return pvPortMalloc(size); }

void operator delete(void *ptr) noexcept { vPortFree(ptr); }
void operator delete(void *ptr, std::size_t size) noexcept { vPortFree(ptr); }
