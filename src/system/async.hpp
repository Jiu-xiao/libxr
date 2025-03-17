#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "semaphore.hpp"
#include "thread.hpp"

namespace LibXR
{

/**
 * @brief 异步任务处理类。
 *        Asynchronous task processing class.
 *
 * 该类用于管理异步任务的执行，提供任务分配、状态管理以及线程执行功能。
 * This class manages the execution of asynchronous tasks, providing task assignment,
 * status management, and thread execution functionalities.
 */
class ASync
{
 public:
  /**
   * @brief 异步任务的状态枚举。
   *        Enumeration of asynchronous task statuses.
   */
  enum class Status : uint8_t
  {
    REDAY,  ///< 任务已准备就绪。 Task is ready.
    BUSY,   ///< 任务正在执行中。 Task is currently running.
    DONE    ///< 任务已完成。 Task is completed.
  };

  /**
   * @brief 构造 `ASync` 对象并初始化任务线程。
   *        Constructs an `ASync` object and initializes the task thread.
   *
   * @param stack_depth 线程栈深度。
   *                    Stack depth for the thread.
   * @param priority 线程优先级。
   *                 Priority of the thread.
   */
  ASync(size_t stack_depth, Thread::Priority priority);

  /**
   * @brief 任务线程函数，等待信号量并执行任务。
   *        Task thread function that waits for a semaphore and executes the assigned job.
   *
   * 该函数作为 `ASync` 任务的主线程，持续等待 `sem_` 释放后执行任务，
   * 执行完成后更新 `status_` 状态。
   * This function serves as the main thread for `ASync`, continuously
   * waiting for `sem_` to be released before executing a job,
   * and updating `status_` upon completion.
   *
   * @param async 指向 `ASync` 实例的指针。
   *              Pointer to the `ASync` instance.
   */
  static void ThreadFun(ASync *async)
  {
    while (true)
    {
      if (async->sem_.Wait() == ErrorCode::OK)
      {
        async->job_.Run(false, async);
        async->status_ = Status::DONE;
      }
    }
  }

  Status status_ =
      Status::REDAY;  ///< 当前异步任务状态。 The current status of the asynchronous task.

  /**
   * @brief 获取当前异步任务的状态。
   *        Retrieves the current status of the asynchronous task.
   *
   * 如果任务尚未完成，则返回当前 `status_`。
   * 如果任务已完成，则返回 `DONE` 并重置状态为 `REDAY`。
   * If the task is not yet completed, it returns the current `status_`.
   * If the task is completed, it returns `DONE` and resets the status to `REDAY`.
   *
   * @return 返回当前任务状态。
   *         Returns the current task status.
   */
  Status GetStatus()
  {
    if (status_ != Status::DONE)
    {
      return status_;
    }
    else
    {
      status_ = Status::REDAY;
      return Status::DONE;
    }
  }

  /**
   * @brief 分配一个异步任务并准备执行。
   *        Assigns an asynchronous job and prepares for execution.
   *
   * 该函数用于设置 `job_` 回调，并将任务状态置为 `BUSY`。
   * This function sets the `job_` callback and updates the task status to `BUSY`.
   *
   * @param job 需要执行的回调任务。
   *            The callback job to be executed.
   * @return 返回 `ErrorCode`，指示操作是否成功。
   *         Returns an `ErrorCode` indicating whether the operation was successful.
   */
  ErrorCode AssignJob(Callback<ASync *> job);

  /**
   * @brief 在回调环境中分配任务，并通知任务线程执行。
   *        Assigns a job from a callback environment and notifies the task thread.
   *
   * 该函数适用于中断上下文或回调环境，
   * 直接修改 `job_` 并设置状态为 `BUSY`，然后通过信号量通知任务线程。
   * This function is designed for use in an interrupt context or callback environment,
   * directly modifying `job_`, setting the status to `BUSY`,
   * and signaling the task thread via a semaphore.
   *
   * @param job 需要执行的回调任务。
   *            The callback job to be executed.
   * @param in_isr 是否在中断上下文中调用。
   *               Indicates whether the function is called within an interrupt service
   * routine.
   */
  void AssignJobFromCallback(Callback<ASync *> job, bool in_isr)
  {
    job_ = job;
    status_ = Status::BUSY;
    sem_.PostFromCallback(in_isr);
  }

  Callback<ASync *>
      job_;  ///< 存储分配的异步任务回调。 Stores the assigned asynchronous job callback.
  Semaphore sem_;  ///< 控制任务执行的信号量。 Semaphore controlling task execution.

  Thread thread_handle_;  ///< 处理异步任务的线程。 Thread handling asynchronous tasks.
};

}  // namespace LibXR
