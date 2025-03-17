#pragma once

#include "libxr_def.hpp"
#include "thread.hpp"

namespace LibXR
{

/**
 * @brief 信号处理类，用于线程间的信号传递和同步。
 *        Signal handling class for inter-thread signaling and synchronization.
 *
 * 该类提供信号的发送、回调环境中的信号发送以及线程等待信号的功能，
 * 主要用于线程间的通信和同步。
 * This class provides functionalities for sending signals,
 * sending signals from a callback environment, and waiting for signals,
 * primarily used for inter-thread communication and synchronization.
 */
class Signal
{
 public:
  /**
   * @brief 触发目标线程的信号处理操作。
   *        Triggers a signal action on the target thread.
   *
   * 该函数用于向指定线程 `thread` 发送信号 `signal`，
   * 使其执行相应的信号处理逻辑。
   * This function sends the signal `signal` to the specified thread `thread`,
   * triggering its corresponding signal handling logic.
   *
   * @param thread 目标线程对象。
   *               The target thread object.
   * @param signal 需要触发的信号。
   *               The signal to be triggered.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns an `ErrorCode` indicating whether the operation was successful.
   */
  static ErrorCode Action(Thread &thread, int signal);

  /**
   * @brief 在回调环境中触发目标线程的信号处理操作。
   *        Triggers a signal action on the target thread from a callback environment.
   *
   * 该函数适用于中断或回调环境，可安全地向指定线程 `thread` 发送信号 `signal`。
   * This function is designed for use in an interrupt or callback environment,
   * safely sending the signal `signal` to the specified thread `thread`.
   *
   * @param thread 目标线程对象。
   *               The target thread object.
   * @param signal 需要触发的信号。
   *               The signal to be triggered.
   * @param in_isr 指示是否在中断上下文中调用。
   *               Indicates whether the function is called within an interrupt service
   * routine.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns an `ErrorCode` indicating whether the operation was successful.
   */
  static ErrorCode ActionFromCallback(Thread &thread, int signal, bool in_isr);

  /**
   * @brief 等待指定信号的到来。
   *        Waits for the specified signal.
   *
   * 该函数阻塞调用线程，直到接收到 `signal` 或者超时。
   * This function blocks the calling thread until it receives `signal` or times out.
   *
   * @param signal 等待的信号编号。
   *               The signal number to wait for.
   * @param timeout 等待超时时间（默认为 `UINT32_MAX`，表示无限等待）。
   *               The timeout duration (default is `UINT32_MAX`, meaning infinite wait).
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns an `ErrorCode` indicating whether the operation was successful.
   */
  static ErrorCode Wait(int signal, uint32_t timeout = UINT32_MAX);
};

}  // namespace LibXR
