#pragma once

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libxr_def.hpp"
#include "signal.hpp"

using namespace LibXR;

ErrorCode Signal::Action(Thread &thread, int signal) {
  signal += SIGRTMIN;
  ASSERT(signal >= SIGRTMIN && signal <= SIGRTMAX);
  if (pthread_kill(thread, signal) == 0) {
    return NO_ERR;
  } else {
    return ERR_FAIL;
  }
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
  ts.tv_nsec = (timeout % 1000) * 1000000;
  int res = sigtimedwait(&waitset, NULL, &ts);
  pthread_sigmask(SIG_BLOCK, &oldset, NULL);
  if (res == -1) {
    if (errno == EAGAIN) {
      return ERR_TIMEOUT;
    } else {
      return ERR_FAIL;
    }
  } else {
    return NO_ERR;
  }
}
