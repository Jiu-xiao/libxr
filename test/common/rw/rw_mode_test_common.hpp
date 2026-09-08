/**
 * @file rw_mode_test_common.hpp
 * @brief RW 完成通知模式与结果检查 / RW completion modes and result checks.
 */
#pragma once

#include <atomic>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace LibXRTest
{

inline constexpr uint32_t ASYNC_TIMEOUT_MS = 200;
inline constexpr uint32_t SHORT_WAIT_MS = 100;

enum class TestMode : uint8_t
{
  NONE,
  POLLING,
  CALLBACK,
  BLOCK
};

inline constexpr TestMode ALL_MODES[] = {TestMode::NONE, TestMode::POLLING,
                                         TestMode::CALLBACK, TestMode::BLOCK};
inline constexpr TestMode ASYNC_MODES[] = {TestMode::NONE, TestMode::POLLING,
                                           TestMode::CALLBACK};

struct CompletionProbe
{
  std::atomic<uint32_t> count{0};
  std::atomic<int> last{static_cast<int>(LibXR::ErrorCode::OK)};
  LibXR::Semaphore sem;

  CompletionProbe() : sem(0) {}

  /**
   * @brief 重置测试记录的状态 / Reset recorded test state.
   */
  void Reset()
  {
    count.store(0, std::memory_order_release);
    last.store(static_cast<int>(LibXR::ErrorCode::OK), std::memory_order_release);
  }
};

template <typename Op>
struct ModeHarness
{
  using CallbackType = typename Op::Callback;
  using PollingStatus = typename Op::OperationPollingStatus;

  explicit ModeHarness(TestMode mode, uint32_t timeout = ASYNC_TIMEOUT_MS)
      : mode(mode), callback(CallbackType::Create(OnCallback, this)), sem(0), op()
  {
    Bind(timeout);
    Reset();
  }

  ModeHarness(const ModeHarness&) = delete;
  ModeHarness& operator=(const ModeHarness&) = delete;

  /**
   * @brief 重置测试记录的状态 / Reset recorded test state.
   */
  void Reset()
  {
    polling_status.store(PollingStatus::READY, std::memory_order_release);
    probe.Reset();
  }

  /**
   * @brief 检查轮询仍在运行或回调尚未触发
   *        / Check running polling status or an uncalled callback.
   */
  void ExpectPendingSubmitted() const
  {
    if (mode == TestMode::POLLING)
    {
      TEST_ASSERT(polling_status.load(std::memory_order_acquire) ==
                  PollingStatus::RUNNING);
    }
    else if (mode == TestMode::CALLBACK)
    {
      TEST_ASSERT(probe.count.load(std::memory_order_acquire) == 0);
    }
  }

  /**
   * @brief 检查完成状态及回调次数 / Check completion status and callback count.
   */
  void ExpectFinal(LibXR::ErrorCode expected)
  {
    switch (mode)
    {
      case TestMode::NONE:
        return;
      case TestMode::POLLING:
        TEST_ASSERT(polling_status.load(std::memory_order_acquire) ==
                    ((expected == LibXR::ErrorCode::OK) ? PollingStatus::DONE
                                                        : PollingStatus::ERROR));
        return;
      case TestMode::CALLBACK:
        TEST_ASSERT(probe.sem.Wait(ASYNC_TIMEOUT_MS) == LibXR::ErrorCode::OK);
        TEST_ASSERT(probe.count.load(std::memory_order_acquire) == 1);
        TEST_ASSERT(static_cast<LibXR::ErrorCode>(
                        probe.last.load(std::memory_order_acquire)) == expected);
        return;
      case TestMode::BLOCK:
        return;
    }
  }

  /**
   * @brief 记录回调结果与次数并通知测试线程
   *        / Record callback result and count, then notify the test thread.
   */
  static void OnCallback(bool in_isr, ModeHarness* self, LibXR::ErrorCode status)
  {
    self->probe.last.store(static_cast<int>(status), std::memory_order_release);
    self->probe.count.fetch_add(1, std::memory_order_acq_rel);
    self->probe.sem.PostFromCallback(in_isr);
  }

  /**
   * @brief 按测试模式设置操作的通知对象
   *        / Bind operation targets for the selected test mode.
   */
  void Bind(uint32_t timeout)
  {
    switch (mode)
    {
      case TestMode::NONE:
        op = Op();
        return;
      case TestMode::POLLING:
        op = Op(polling_status);
        return;
      case TestMode::CALLBACK:
        op = Op(callback);
        return;
      case TestMode::BLOCK:
        op = Op(sem, timeout);
        return;
    }
  }

  TestMode mode;
  std::atomic<PollingStatus> polling_status{PollingStatus::READY};
  CompletionProbe probe;
  CallbackType callback;
  LibXR::Semaphore sem;
  Op op;
};

using ReadHarness = ModeHarness<LibXR::ReadOperation>;
using WriteHarness = ModeHarness<LibXR::WriteOperation>;

}  // namespace LibXRTest
