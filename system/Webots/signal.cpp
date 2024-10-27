#include "signal.hpp"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libxr_def.hpp"

using namespace LibXR;

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
  clock_gettime(CLOCK_REALTIME, &ts);

  uint32_t add = 0;
  long raw_time = 1U * 1000U * 1000U + ts.tv_nsec;
  add = raw_time / (1000U * 1000U * 1000U);

  ts.tv_sec += add;
  ts.tv_nsec = raw_time % (1000U * 1000U * 1000U);

  int res = sigtimedwait(&waitset, NULL, &ts);

  while (_libxr_webots_time_count - start_time < timeout) {
    res = !sigtimedwait(&waitset, NULL, &ts);
    if (res) {
      return ErrorCode::OK;
    }
  }
  pthread_sigmask(SIG_BLOCK, &oldset, NULL);
  return res == signal ? ErrorCode::OK : ErrorCode::TIMEOUT;
}
