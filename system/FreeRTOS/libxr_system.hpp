#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

namespace LibXR {
typedef SemaphoreHandle_t libxr_mutex_handle;
typedef SemaphoreHandle_t libxr_semaphore_handle;
typedef TaskHandle_t libxr_thread_handle;
typedef SemaphoreHandle_t condition_var_handle;

void PlatformInit();  // NOLINT
}  // namespace LibXR
