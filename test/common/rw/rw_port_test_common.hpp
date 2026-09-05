/**
 * @file rw_port_test_common.hpp
 * @brief `ReadPort` / `WritePort` 状态机测试 helper。 Shared state-machine helpers for
 * `ReadPort` / `WritePort` tests.
 * @details 测试项目：
 *          1. 提供 `PENDING` / 失败回调桩和阻塞调用线程封装。
 *          2. 提供 `ReadPort` / `WritePort` 队列完成、失败清理与阻塞唤醒验证 helper。
 *          3. 提供带计数的 `TrackingReadPort`，用于核对 `OnRxDequeue` 的调用次数。
 *          Test items:
 *          1. Provide pending/failure stubs and blocking-call thread wrappers.
 *          2. Provide helpers for completion, fail-clear, and waiter wake-up scenarios.
 *          3. Provide a counting `TrackingReadPort` for verifying `OnRxDequeue` calls.
 */
#pragma once

#include <cstring>
#include <vector>

#include "rw_thread_test_common.hpp"

namespace
{
void PendingWriteFun(LibXR::WritePort&, bool) {}
struct TrackingReadPort : LibXR::ReadPort
{
  using LibXR::ReadPort::ReadPort;
  void OnReadQueueSpaceAvailable(bool) override { dequeue_count++; }

  uint32_t dequeue_count = 0;
};

/**
 * @brief 辅助函数 `VerifyPendingReadMode`。 Helper function `VerifyPendingReadMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void VerifyPendingReadMode(TestMode mode)
{
  using namespace LibXR;

  ReadPort r(16);

  std::vector<uint8_t> tx = {0x42, 0x73, 0x8A, 0xC1};
  std::vector<uint8_t> rx(4, 0x7A);
  ReadHarness read(mode);

  if (mode == TestMode::BLOCK)
  {
    Semaphore done;
    Thread finisher;
    StartReadQueueCompleter(finisher, r, done, tx.data(), tx.size(), "rd_queue");

    auto block_result = r(RawData{rx.data(), rx.size()}, read.op);
    ASSERT(block_result == ErrorCode::OK);

    ExpectWaitOk(done);
    JoinThreadIfNeeded(finisher);
    ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    return;
  }

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);
  ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  {
    auto queue = r.GetReadQueue(false);
    ASSERT(queue.PushBatch(tx.data(), tx.size()) == ErrorCode::OK);
    queue.Publish();
  }
  if (mode != TestMode::NONE)
  {
    read.ExpectFinal(ErrorCode::OK);
  }
  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
}

/**
 * @brief 辅助函数 `VerifyPendingWriteMode`。 Helper function `VerifyPendingWriteMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void VerifyPendingWriteMode(TestMode mode, LibXR::ErrorCode result)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  std::vector<uint8_t> tx = {0x31, 0x41, 0x59, 0x26};
  WriteHarness write(mode);

  if (mode == TestMode::BLOCK)
  {
    Semaphore done;
    Thread finisher;
    StartWriteFinisher(finisher, w, done, result, "wr_finish");

    auto block_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
    ASSERT(block_result == result);

    ExpectWaitOk(done);
    JoinThreadIfNeeded(finisher);
    ASSERT(w.Size() == 0);
    return;
  }

  auto call_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
  ASSERT(call_result == ErrorCode::OK);

  {
    auto queue = w.GetWriteQueue(false);
    ASSERT(!queue.Empty());
    if (result == ErrorCode::OK)
    {
      static uint8_t sink[16];
      ASSERT(queue.AvailableSize() <= sizeof(sink));
      queue.PopAll(sink);
    }
    else
    {
      queue.FailFront(result);
    }
  }
  if (mode != TestMode::NONE)
  {
    write.ExpectFinal(result);
  }
  ASSERT(w.Size() == 0);
}

/**
 * @brief 辅助函数 `VerifyPendingReadFailAndClearMode`。 Helper function
 * `VerifyPendingReadFailAndClearMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void VerifyPendingReadFailAndClearMode(TestMode mode, LibXR::ErrorCode reason)
{
  using namespace LibXR;

  ReadPort r(16);

  uint8_t rx[4] = {0x7A, 0x7B, 0x7C, 0x7D};
  static const uint8_t STALE_EXPECT[] = {0x7A, 0x7B, 0x7C, 0x7D};
  ReadHarness read(mode);

  auto call_result = r(RawData{rx, sizeof(rx)}, read.op);
  ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  ASSERT(r.ClearQueuedData(false) == ErrorCode::OK);

  if (mode != TestMode::NONE)
  {
    read.ExpectFinal(reason);
  }
  ASSERT(std::memcmp(rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  ASSERT(r.Size() == 0);
}

/**
 * @brief 辅助函数 `VerifyPendingWriteFailAndClearMode`。 Helper function
 * `VerifyPendingWriteFailAndClearMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void VerifyPendingWriteFailAndClearMode(TestMode mode, LibXR::ErrorCode reason)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x31, 0x41, 0x59, 0x26};
  WriteHarness write(mode);

  auto call_result = w(ConstRawData{TX, sizeof(TX)}, write.op);
  ASSERT(call_result == ErrorCode::OK);
  write.ExpectPendingSubmitted();

  {
    auto queue = w.GetWriteQueue(false);
    ASSERT(!queue.Empty());
    queue.FailFront(reason);
  }

  if (mode != TestMode::NONE)
  {
    write.ExpectFinal(reason);
  }
  ASSERT(w.Size() == 0);
}

}  // namespace
