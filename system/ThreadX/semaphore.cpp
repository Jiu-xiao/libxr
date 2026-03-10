#include "semaphore.hpp"

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "timer.hpp"
#include "tx_api.h"

using namespace LibXR;

Semaphore::Semaphore(uint32_t init_count)
{
  tx_semaphore_create(&semaphore_handle_, const_cast<char*>("xr_sem"), init_count);
}

Semaphore::~Semaphore() { tx_semaphore_delete(&semaphore_handle_); }

void Semaphore::Post() { tx_semaphore_put(&semaphore_handle_); }

ErrorCode Semaphore::Wait(uint32_t timeout)
{
  ULONG tx_timeout;

  if (timeout == 0)
  {
    tx_timeout = TX_NO_WAIT;
  }
  else
  {
    tx_timeout = timeout * TX_TIMER_TICKS_PER_SECOND / 1000;
    if (tx_timeout == 0) tx_timeout = 1;
  }

  UINT status = tx_semaphore_get(&semaphore_handle_, tx_timeout);

  if (status == TX_SUCCESS)
  {
    return ErrorCode::OK;
  }
  else if (status == TX_NO_INSTANCE || status == TX_NOT_AVAILABLE ||
           status == TX_WAIT_ABORTED)
  {
    return ErrorCode::TIMEOUT;
  }

  return ErrorCode::FAILED;
}

void Semaphore::PostFromCallback(bool in_isr)
{
  UNUSED(in_isr);
  tx_semaphore_put(&semaphore_handle_);
}

size_t Semaphore::Value()
{
  ULONG count;
  tx_semaphore_info_get(&semaphore_handle_, NULL, &count, NULL, NULL, NULL);
  return static_cast<size_t>(count);
}
