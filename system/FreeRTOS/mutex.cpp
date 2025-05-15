#include "mutex.hpp"

#include "libxr_system.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(xSemaphoreCreateMutex()) {}

Mutex::~Mutex()
{
  if (mutex_handle_)
  {
    vSemaphoreDelete(mutex_handle_);
  }
}

ErrorCode Mutex::Lock()
{
  if (xSemaphoreTake(mutex_handle_, portMAX_DELAY) != pdPASS)
  {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

ErrorCode Mutex::TryLock()
{
  if (xSemaphoreTake(mutex_handle_, 0) != pdPASS)
  {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

void Mutex::Unlock() { xSemaphoreGive(mutex_handle_); }
