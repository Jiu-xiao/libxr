#pragma once

#include <pthread.h>
#include <semaphore.h>

namespace LibXR
{
typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t* libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;

/**
 * @brief  平台初始化函数
 *         Platform initialization function
 *
 * @details
 * 该函数用于初始化 POSIX 线程相关的资源，例如互斥锁、信号量和条件变量。
 * 在使用 `LibXR` 线程库之前，应调用该函数进行必要的系统初始化。
 *
 * This function initializes POSIX thread-related resources such as mutexes,
 * semaphores, and condition variables. It should be called before using the
 * `LibXR` threading library for proper system setup.
 */
void PlatformInit();
}  // namespace LibXR
