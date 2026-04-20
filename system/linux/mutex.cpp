#include "mutex.hpp"

#include <pthread.h>

#include <cerrno>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(PTHREAD_MUTEX_INITIALIZER) {}

Mutex::~Mutex() { pthread_mutex_destroy(&mutex_handle_); }

ErrorCode Mutex::Lock()
{
  const int ans = pthread_mutex_lock(&mutex_handle_);
  if (ans != 0)
  {
    return ErrorCode::FAILED;
  }
  return ErrorCode::OK;
}

ErrorCode Mutex::TryLock()
{
  const int ans = pthread_mutex_trylock(&mutex_handle_);
  if (ans == EBUSY)
  {
    return ErrorCode::BUSY;
  }
  if (ans != 0)
  {
    return ErrorCode::FAILED;
  }
  return ErrorCode::OK;
}

void Mutex::Unlock() { pthread_mutex_unlock(&mutex_handle_); }
