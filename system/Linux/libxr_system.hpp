#pragma once

#include <pthread.h>
#include <semaphore.h>

namespace LibXR {
typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} condition_var_handle;

void PlatformInit();
}  // namespace LibXR
