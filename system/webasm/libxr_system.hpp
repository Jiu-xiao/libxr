#pragma once

#include <stdint.h>

namespace LibXR
{
typedef uint32_t libxr_mutex_handle;
typedef uint32_t libxr_semaphore_handle;
typedef uint32_t libxr_thread_handle;

/**
 * @brief  平台初始化函数，执行必要的系统初始化
 *         Platform initialization function for necessary system setup
 *
 * @details
 * 此函数用于初始化底层平台的线程管理、同步机制（如互斥锁、信号量）以及其他系统资源，
 * 需要在使用 `LibXR` 相关功能之前调用，以确保所有系统组件正确初始化。
 *
 * This function initializes the underlying platform's thread management,
 * synchronization mechanisms (such as mutexes and semaphores), and other system
 * resources. It should be called before using any `LibXR` functionalities to ensure
 * proper setup.
 */
void PlatformInit();  // NOLINT
}  // namespace LibXR
