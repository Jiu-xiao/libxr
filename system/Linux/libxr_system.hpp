#pragma once

#include <pthread.h>
#include <semaphore.h>

#include <cstdint>

namespace LibXR
{
typedef pthread_mutex_t libxr_mutex_handle;
typedef sem_t* libxr_semaphore_handle;
typedef pthread_t libxr_thread_handle;

/**
 * @brief  平台初始化函数
 *         Platform initialization function
 *
 * @param  timer_pri  定时器任务的优先级（默认值 2）
 *                    Timer task priority (default: 2)
 * @param  timer_stack_depth  定时器任务的栈深度（默认值 65536）
 *                            Timer task stack depth (default: 65536)
 *
 * @details
 * 该函数用于初始化 POSIX 线程相关的资源，例如互斥锁、信号量和条件变量。
 * 在使用 `LibXR` 线程库之前，应调用该函数进行必要的系统初始化。
 *
 * This function initializes POSIX thread-related resources such as mutexes,
 * semaphores, and condition variables. It should be called before using the
 * `LibXR` threading library for proper system setup.
 *
 */
void PlatformInit(uint32_t timer_pri = 2, uint32_t timer_stack_depth = 65536);
}  // namespace LibXR
