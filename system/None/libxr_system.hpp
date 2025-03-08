#pragma once

#include <stdint.h>

namespace LibXR {
typedef uint32_t libxr_mutex_handle;
typedef uint32_t libxr_semaphore_handle;
typedef uint32_t libxr_thread_handle;
typedef uint32_t condition_var_handle;

void PlatformInit();  // NOLINT
}  // namespace LibXR
