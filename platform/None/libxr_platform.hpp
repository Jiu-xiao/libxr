#pragma once

#include <stdint.h>

namespace LibXR {
typedef uint32_t libxr_mutex_handle;
typedef uint32_t libxr_semaphore_handle;
typedef uint32_t libxr_thread_handle;
typedef uint32_t condition_var_handle;

extern "C" {
extern uint32_t libxr_get_time_ms();

extern uint64_t libxr_get_time();
}

void PlatformInit();
}  // namespace LibXR
