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

void LibXR::PlatformInit() {}

void LibXR::Timer::RefreshTimerInIdle() {
  static bool in_timer = false;
  if (in_timer) {
    return;
  }

  static auto last_refresh_time = Timebase::GetMilliseconds();

  if (last_refresh_time == Timebase::GetMilliseconds()) {
    return;
  }

  in_timer = true;
  last_refresh_time++;
  Timer::Refresh();
  in_timer = false;
}
