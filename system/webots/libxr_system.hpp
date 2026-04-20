#pragma once

#include <pthread.h>
#include <semaphore.h>

#include <cstdint>
#include <webots/Robot.hpp>

/**
 * @brief  Webots 仿真时间计数变量
 *         Webots simulation time counter variable
 */
extern uint64_t _libxr_webots_time_count;  // NOLINT

/**
 * @brief  Webots 机器人句柄
 *         Webots robot handle
 */
extern webots::Robot *_libxr_webots_robot_handle;  // NOLINT

namespace LibXR
{

/**
 * @brief  互斥锁句柄类型定义
 *         Mutex handle type definition
 */
typedef pthread_mutex_t libxr_mutex_handle;

/**
 * @brief  信号量句柄类型定义
 *         Semaphore handle type definition
 */
typedef sem_t *libxr_semaphore_handle;

/**
 * @brief  线程句柄类型定义
 *         Thread handle type definition
 */
typedef pthread_t libxr_thread_handle;

#ifndef __DOXYGEN__
/**
 * @brief  条件变量结构体定义，包含互斥锁和条件变量
 *         Condition variable structure definition including a mutex and a condition
 * variable
 */
typedef struct
{
  pthread_mutex_t mutex;  ///< 互斥锁 Mutex
  pthread_cond_t cond;    ///< 条件变量 Condition variable
} condition_var_handle;
#endif

/**
 * @brief  Webots 平台初始化函数
 *         Webots platform initialization function
 * @param  robot Webots 机器人对象指针 Webots robot object pointer
 * @param  timer_pri  定时器任务的优先级（默认值 2）
 *                    Timer task priority (default: 2)
 * @param  timer_stack_depth  定时器任务的栈深度（默认值 65536）
 *                            Timer task stack depth (default: 65536)
 *
 * @details
 * 此函数用于初始化 Webots 仿真环境，设置 `_libxr_webots_robot_handle` 变量，
 * 并对仿真时间计数变量 `_libxr_webots_time_count` 进行初始化。
 *
 * This function initializes the Webots simulation environment by setting
 * the `_libxr_webots_robot_handle` variable and initializing the simulation
 * time counter `_libxr_webots_time_count`.
 */
void PlatformInit(webots::Robot *robot = nullptr, uint32_t timer_pri = 2,
                  uint32_t timer_stack_depth = 65536);  // NOLINT

}  // namespace LibXR
