/**
 * @file rw_mode_test_common.hpp
 * @brief `rw` / `pipe` 模式与等待器测试 helper。 Shared mode and waiter helpers for `rw` / `pipe` tests.
 * @details 测试项目：
 *          1. 统一封装 `NONE` / `POLLING` / `CALLBACK` / `BLOCK` 四类操作模式。
 *          2. 统一维护异步回调计数、最终错误码和等待信号量。
 *          3. 统一提供等待断言，避免各测试文件重复展开样板代码。
 *          Test items:
 *          1. Provide one shared wrapper for `NONE` / `POLLING` / `CALLBACK` / `BLOCK` modes.
 *          2. Keep callback counts, final status, and semaphores in one reusable probe.
 *          3. Centralize wait assertions so scenario files stay focused.
 */
#pragma once

#include <atomic>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"

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
   * @brief 辅助函数 `Reset`。 Helper function `Reset`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
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
   * @brief 辅助函数 `Reset`。 Helper function `Reset`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  void Reset()
  {
    polling_status = PollingStatus::READY;
    probe.Reset();
  }

  /**
   * @brief 断言辅助函数 `ExpectPendingSubmitted`。 Assertion helper function `ExpectPendingSubmitted`.
   * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
   *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
   */
  void ExpectPendingSubmitted() const
  {
    // 辅助内容：验证当前失败或退出预期。
    // Helper coverage: validate the current expected failure or exit condition.
    if (mode == TestMode::POLLING)
    {
      ASSERT(polling_status == PollingStatus::RUNNING);
    }
    else if (mode == TestMode::CALLBACK)
    {
      ASSERT(probe.count.load(std::memory_order_acquire) == 0);
    }
  }

  /**
   * @brief 断言辅助函数 `ExpectFinal`。 Assertion helper function `ExpectFinal`.
   * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
   *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
   */
  void ExpectFinal(LibXR::ErrorCode expected)
  {
    // 辅助内容：验证当前失败或退出预期。
    // Helper coverage: validate the current expected failure or exit condition.
    switch (mode)
    {
      case TestMode::NONE:
        return;
      case TestMode::POLLING:
        ASSERT(polling_status == ((expected == LibXR::ErrorCode::OK)
                                      ? PollingStatus::DONE
                                      : PollingStatus::ERROR));
        return;
      case TestMode::CALLBACK:
        ASSERT(probe.sem.Wait(ASYNC_TIMEOUT_MS) == LibXR::ErrorCode::OK);
        ASSERT(probe.count.load(std::memory_order_acquire) == 1);
        ASSERT(static_cast<LibXR::ErrorCode>(
                   probe.last.load(std::memory_order_acquire)) == expected);
        return;
      case TestMode::BLOCK:
        return;
    }
  }

  /**
   * @brief 辅助函数 `OnCallback`。 Helper function `OnCallback`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  static void OnCallback(bool in_isr, ModeHarness* self, LibXR::ErrorCode status)
  {
    self->probe.last.store(static_cast<int>(status), std::memory_order_release);
    self->probe.count.fetch_add(1, std::memory_order_acq_rel);
    self->probe.sem.PostFromCallback(in_isr);
  }

  /**
   * @brief 辅助函数 `Bind`。 Helper function `Bind`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
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
  PollingStatus polling_status = PollingStatus::READY;
  CompletionProbe probe;
  CallbackType callback;
  LibXR::Semaphore sem;
  Op op;
};

using ReadHarness = ModeHarness<LibXR::ReadOperation>;
using WriteHarness = ModeHarness<LibXR::WriteOperation>;

}  // namespace LibXRTest
