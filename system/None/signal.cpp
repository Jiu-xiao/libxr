#include "signal.hpp"

#include "libxr_def.hpp"
#include "timer.hpp"

using namespace LibXR;

static uint32_t sig;

ErrorCode Signal::Action(Thread &thread, int signal) {
  UNUSED(thread);
  ASSERT(signal > 0 && signal < 32);

  sig |= 1 << signal;
  return ErrorCode::OK;
}

ErrorCode Signal::ActionFromCallback(Thread &thread, int signal, bool in_isr) {
  UNUSED(in_isr);
  return Action(thread, signal);
}

ErrorCode Signal::Wait(int signal, uint32_t timeout) {
  ASSERT(signal > 0 && signal < 32);
  uint32_t flag = (1 << signal);

  if ((sig | flag) == flag) {
    sig &= ~flag;
    return ErrorCode::OK;
  } else if (timeout == 0) {
    return ErrorCode::TIMEOUT;
  }

  uint32_t now = Timebase::GetMilliseconds();

  while (Timebase::GetMilliseconds() - now < timeout) {
    if ((sig | flag) == flag) {
      sig &= ~flag;
      return ErrorCode::OK;
    }

    Timer::RefreshTimerInIdle();
  }
  return ErrorCode::TIMEOUT;
}
