#include "mutex.hpp"

#include "libxr_system.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include "tx_api.h"

using namespace LibXR;

Mutex::Mutex()
{
  tx_mutex_create(&mutex_handle_, const_cast<char*>("xr_mutex"), TX_NO_INHERIT);
}

Mutex::~Mutex() { tx_mutex_delete(&mutex_handle_); }

ErrorCode Mutex::Lock()
{
  UINT status = tx_mutex_get(&mutex_handle_, TX_WAIT_FOREVER);
  return (status == TX_SUCCESS) ? ErrorCode::OK : ErrorCode::FAILED;
}

ErrorCode Mutex::TryLock()
{
  UINT status = tx_mutex_get(&mutex_handle_, TX_NO_WAIT);
  if (status == TX_SUCCESS)
  {
    return ErrorCode::OK;
  }
  else if (status == TX_NOT_AVAILABLE)
  {
    return ErrorCode::BUSY;
  }
  return ErrorCode::FAILED;
}

void Mutex::Unlock() { tx_mutex_put(&mutex_handle_); }
