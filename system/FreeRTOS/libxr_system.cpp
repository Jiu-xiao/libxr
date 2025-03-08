#include "libxr.hpp"

void LibXR::PlatformInit() {
  if (Timebase::timebase == nullptr) {
    /* You should initialize Timebase first */
    ASSERT(false);
  }

  uint32_t time_need_to_catch_up =
      Timebase::GetMilliseconds() - xTaskGetTickCount();

  if (time_need_to_catch_up > 0) {
    xTaskCatchUpTicks(time_need_to_catch_up);
  }
}

void *operator new(std::size_t size) {
  auto ans = pvPortMalloc(size);
  ASSERT(ans != nullptr);
  return ans;
}

void operator delete(void *ptr) noexcept { vPortFree(ptr); }
void operator delete(void *ptr, std::size_t size) noexcept {
  UNUSED(size);
  vPortFree(ptr);
}
