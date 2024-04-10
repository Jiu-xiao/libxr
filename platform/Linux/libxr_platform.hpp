#pragma once

#include <pthread.h>
#include <semaphore.h>

namespace LibXR {
typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;
#define libxr_queue_handle                                                     \
  struct {                                                                     \
    std::array<Data, Length> data;                                             \
    std::size_t head = 0;                                                      \
    std::size_t tail = 0;                                                      \
    bool is_full = false;                                                      \
    Mutex mutex;                                                               \
    Semaphore sem;                                                             \
  }

void PlatformInit();
} // namespace LibXR