/**
 * @file transfer_wait.hpp
 * @brief 驱动测试共用的操作完成等待 / Operation completion waits shared by driver tests.
 *
 * 复用阻塞、轮询和回调通知，检查错误、超时及观测到的回调次数。
 * Reuse blocking, polling and callback notifications; check errors, timeouts and
 * observed callback counts. This helper does not submit transfers by itself.
 */
#pragma once

#include <atomic>

#include "operation.hpp"
#include "test_assert.hpp"
#include "thread.hpp"

namespace LibXR::Test::Detail
{
/** @brief 一组可复用的测试完成通知 / Reusable completion notifications for tests. */
class TransferTestCompletion
{
  using Transfer = Operation<ErrorCode>;
  using Status = Transfer::OperationPollingStatus;
  using Mode = Transfer::OperationType;

 public:
  explicit TransferTestCompletion(uint32_t timeout_ms)
      : callback_(Transfer::Callback::Create(
            [](bool, TransferTestCompletion* self, ErrorCode result)
            {
              if (result != ErrorCode::OK)
              {
                self->failed_.store(1, std::memory_order_relaxed);
              }
              // 最后一次访问上下文才发布完成；ISR 中不打印或断言。
              // Publish on the last context access; no logging or assertions in ISR.
              self->calls_.fetch_add(1, std::memory_order_release);
            },
            this)),
        operations_{Transfer(semaphore_, timeout_ms), Transfer(status_),
                    Transfer(callback_)},
        timeout_ms_(timeout_ms)
  {
    TEST_ASSERT(timeout_ms > 0 && timeout_ms < UINT32_MAX / 2U);
  }

  template <typename Submit>
  void Run(unsigned mode_index, Submit submit)
  {
    auto& operation = operations_[mode_index];
    const Mode mode = operation.type;
    Check();
    status_.store(Status::READY, std::memory_order_relaxed);
    if (mode == Mode::CALLBACK)
    {
      ++expected_calls_;
    }
    TEST_ASSERT(submit(operation) == ErrorCode::OK);
    const uint32_t start = Thread::GetTime();
    for (;;)
    {
      bool done = mode == Mode::BLOCK;
      if (mode == Mode::POLLING)
      {
        const auto status = status_.load(std::memory_order_acquire);
        TEST_ASSERT(status != Status::ERROR);
        done = status == Status::DONE;
      }
      else if (mode == Mode::CALLBACK)
      {
        const uint32_t calls = calls_.load(std::memory_order_acquire);
        TEST_ASSERT(calls <= expected_calls_);
        done = calls == expected_calls_;
      }
      TEST_ASSERT(failed_.load(std::memory_order_relaxed) == 0);
      // 同步完成时直接继续；不要求必须观察到 RUNNING。
      // Continue immediately for inline completion; observing RUNNING is not required.
      if (done)
      {
        break;
      }
      TEST_ASSERT(Thread::GetTime() - start < timeout_ms_);
      Thread::Sleep(1);
    }
    Check();
  }

  void Check() const
  {
    TEST_ASSERT(calls_.load(std::memory_order_acquire) == expected_calls_);
    TEST_ASSERT(failed_.load(std::memory_order_relaxed) == 0);
  }

 private:
  Semaphore semaphore_;
  std::atomic<Status> status_{Status::READY};
  std::atomic<uint32_t> calls_{0};
  std::atomic<uint32_t> failed_{0};
  Transfer::Callback callback_;
  Transfer operations_[3];
  uint32_t timeout_ms_;
  uint32_t expected_calls_ = 0;
};
}  // namespace LibXR::Test::Detail
