#pragma once

#include <pthread.h>
#include <semaphore.h>

#include <cstdint>
#include <webots/Robot.hpp>

extern uint64_t _libxr_webots_time_count;          // NOLINT
extern webots::Robot *_libxr_webots_robot_handle;  // NOLINT

namespace LibXR {
typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} condition_var_handle;

void PlatformInit(webots::Robot *robot);  // NOLINT
}  // namespace LibXR
