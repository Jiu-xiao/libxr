#include "signal.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

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

  sigemptyset(&waitset);
  sigaddset(&waitset, signal);
  pthread_sigmask(SIG_BLOCK, &waitset, &oldset);

  struct timespec ts;
  ts.tv_sec = timeout / 1000;
  ts.tv_nsec = static_cast<__syscall_slong_t>((timeout % 1000) * 1000000);
  int res = sigtimedwait(&waitset, nullptr, &ts);
  pthread_sigmask(SIG_BLOCK, &oldset, nullptr);
  if (res == -1) {
    if (errno == EAGAIN) {
      return ErrorCode::TIMEOUT;
    } else {
      return ErrorCode::FAILED;
    }
  } else {
    return ErrorCode::OK;
  }
}
