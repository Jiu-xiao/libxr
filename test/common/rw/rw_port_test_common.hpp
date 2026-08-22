/**
 * @file rw_port_test_common.hpp
 * @brief `ReadPort` / `WritePort` 状态机测试 helper。 Shared state-machine helpers for
 * `ReadPort` / `WritePort` tests.
 * @details 测试项目：
 *          1. 提供 `PENDING` / 失败回调桩和阻塞调用线程封装。
 *          2. 提供 `ReadPort` / `WritePort` 队列完成与阻塞唤醒验证 helper。
 *          3. 提供带计数的 `TrackingReadPort`，用于核对 `OnRxDequeue` 的调用次数。
 *          Test items:
 *          1. Provide write pending/failure stubs and blocking-call thread wrappers.
 *          2. Provide helpers for completion and waiter wake-up scenarios.
 *          3. Provide a counting `TrackingReadPort` for verifying `OnRxDequeue` calls.
 */
#pragma once

#include <cstring>
#include <vector>

#include "rw_thread_test_common.hpp"
#include "serialized_service.hpp"

namespace
{
void PendingWriteFun(LibXR::WritePort&, bool) {}

struct SynchronousWritePort : LibXR::WritePort
{
  SynchronousWritePort(size_t queue_size = 2, size_t buffer_size = 64)
      : WritePort(queue_size, buffer_size)
  {
    WritePort::operator=(HandleWrite);
  }

  static void HandleWrite(LibXR::WritePort& base, bool in_isr)
  {
    auto& port = static_cast<SynchronousWritePort&>(base);
    (void)port.service.Invoke(
        1U,
        [&port, in_isr](uint32_t) noexcept
        {
          while (true)
          {
            auto queue = port.GetWriteQueue(in_isr);
            const size_t offered = queue.front_size + queue.next_size;
            if (offered == 0U)
            {
              return;
            }

            if (static_cast<int8_t>(port.result) < 0)
            {
              REQUIRE(queue.FailFront(port.result));
              continue;
            }

            REQUIRE(port.result == LibXR::ErrorCode::OK);
            REQUIRE(offered <= sizeof(port.payload));
            port.payload_size = queue.PopBatch(port.payload, offered);
            REQUIRE(port.payload_size == offered);
          }
        });
  }

  LibXR::SerializedService service;
  LibXR::ErrorCode result = LibXR::ErrorCode::OK;
  uint8_t payload[64]{};
  size_t payload_size = 0;
};

void FailWriteFun(LibXR::WritePort& port, bool in_isr)
{
  auto queue = port.GetWriteQueue(in_isr);
  if (queue.front_size != 0U)
  {
    REQUIRE(queue.FailFront(LibXR::ErrorCode::INIT_ERR));
  }
}

struct TrackingReadPort : LibXR::ReadPort
{
  using LibXR::ReadPort::ReadPort;

  void OnRxDequeue(bool) override { dequeue_count++; }

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
  ReadHarness read(mode, BLOCK_OPERATION_TIMEOUT_MS);

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);
  ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  auto queue = r.GetReadQueue();
  ASSERT(queue.PushBatch(tx.data(), tx.size()) == ErrorCode::OK);
  queue.Publish();
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

  auto call_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
  ASSERT(call_result == ErrorCode::OK);

  ASSERT(CompleteFrontWrite(w, result));
  if (mode != TestMode::NONE)
  {
    write.ExpectFinal(result);
  }
  ASSERT(w.Size() == 0U);
}

}  // namespace
