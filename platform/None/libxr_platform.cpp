#include "libxr_platform.hpp"

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"

void LibXR::PlatformInit() {}

void LibXR::Timer::RefreshTimerInIdle() {
  static bool in_timer = false;
  if (in_timer) {
    return;
  }

  static auto last_refresh_time = libxr_get_time_ms();

  if (last_refresh_time == libxr_get_time_ms()) {
    return;
  }

  in_timer = true;
  last_refresh_time++;
  Timer::Refresh(Thread::Priority::IDLE);
  in_timer = false;
}
