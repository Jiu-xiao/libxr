#include "signal.hpp"

#include "libxr_def.hpp"

using namespace LibXR;

ErrorCode Signal::Action(Thread &thread, int signal) {
  if (xTaskNotify(libxr_thread_handle(thread), 1 << signal, eSetBits) ==
      pdPASS) {
    return ErrorCode::OK;
  } else {
    return ErrorCode::FAILED;
  }
}

ErrorCode Signal::ActionFromCallback(Thread &thread, int signal, bool in_isr) {
  if (in_isr) {
    BaseType_t px_higher_priority_task_woken = 0;
    BaseType_t ans =
        xTaskNotifyFromISR(libxr_thread_handle(thread), 1 << signal, eSetBits,
                           &px_higher_priority_task_woken);
    if (px_higher_priority_task_woken != pdFALSE) {
      portYIELD();
    }
    if (ans == pdPASS) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::FAILED;
    }
  } else {
    return Action(thread, signal);
  }
}

ErrorCode Signal::Wait(int signal, uint32_t timeout) {
  ASSERT(signal >= 0 && signal < 32);

  uint32_t value = 0;
  xTaskNotifyAndQuery(xTaskGetCurrentTaskHandle(), 0, eNoAction, &value);

  const uint32_t SIG_BIT = 1 << signal;

  if ((value & SIG_BIT) == SIG_BIT) {
    value &= ~SIG_BIT;
    xTaskNotify(xTaskGetCurrentTaskHandle(), value, eSetValueWithOverwrite);
    return ErrorCode::OK;
  } else {
    if (timeout == 0) {
      return ErrorCode::TIMEOUT;
    }
  }

  uint32_t current_time = xTaskGetTickCount();

  while (xTaskNotifyWait(0, SIG_BIT, &value, timeout) == pdPASS) {
    if ((value & SIG_BIT) == SIG_BIT) {
      return ErrorCode::OK;
    }

    uint32_t now = xTaskGetTickCount();

    if (now - current_time >= timeout) {
      return ErrorCode::TIMEOUT;
    }

    timeout -= now - current_time;
  }

  return ErrorCode::FAILED;
}
