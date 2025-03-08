#include "signal.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include "libxr_def.hpp"

using namespace LibXR;

extern uint64_t _libxr_webots_time_count;  // NOLINT

ErrorCode Signal::Action(Thread &thread, int signal) {
  signal += SIGRTMIN;
  ASSERT(signal >= SIGRTMIN && signal <= SIGRTMAX);

  if (pthread_kill(thread, signal) == 0) {
    return ErrorCode::OK;
  } else {
    return ErrorCode::FAILED;
  }
}

ErrorCode Signal::ActionFromCallback(Thread &thread, int signal, bool in_isr) {
  UNUSED(in_isr);
  return Action(thread, signal);
}

ErrorCode Signal::Wait(int signal, uint32_t timeout) {
  sigset_t waitset, oldset;
  signal += SIGRTMIN;
  ASSERT(signal >= SIGRTMIN && signal <= SIGRTMAX);

  uint32_t start_time = _libxr_webots_time_count;

  sigemptyset(&waitset);
  sigaddset(&waitset, signal);
  pthread_sigmask(SIG_BLOCK, &waitset, &oldset);

  struct timespec ts;
  UNUSED(clock_gettime(CLOCK_REALTIME, &ts));

  uint32_t add = 0;
  int64_t raw_time =
      static_cast<__syscall_slong_t>(1U * 1000U * 1000U) + ts.tv_nsec;
  add = raw_time / (static_cast<int64_t>(1000U * 1000U * 1000U));

  ts.tv_sec += add;
  ts.tv_nsec = raw_time % (static_cast<int64_t>(1000U * 1000U * 1000U));

  int res = sigtimedwait(&waitset, nullptr, &ts);

  while (_libxr_webots_time_count - start_time < timeout) {
    res = !sigtimedwait(&waitset, nullptr, &ts);
    if (res) {
      return ErrorCode::OK;
    }
  }
  pthread_sigmask(SIG_BLOCK, &oldset, nullptr);
  return res == signal ? ErrorCode::OK : ErrorCode::TIMEOUT;
}
