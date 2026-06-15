/**
 * @file pipe_stream_test_common.hpp
 * @brief `Pipe::Stream` 阻塞提交语义测试 helper。 Shared blocking-stream helpers for `Pipe::Stream` tests.
 * @details 测试项目：
 *          1. 提供阻塞 `Stream` 提交结果传播 helper。
 *          2. 提供析构自动提交与超时回收 helper。
 *          Test items:
 *          1. Provide helpers for blocking `Stream` result propagation.
 *          2. Provide helpers for destructor auto-commit and timeout cleanup.
 */
#pragma once

#include "pipe_transfer_test_common.hpp"

namespace
{
enum class StreamSubmitMode : uint8_t
{
  COMMIT,
  DESTRUCT
};

void VerifyStreamBlockPendingCompletion(LibXR::ErrorCode finish_result,
                                        LibXR::ErrorCode expected_result,
                                        StreamSubmitMode submit_mode)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x41, 0x42, 0x43, 0x44};
  Semaphore sem;
  WriteOperation op(sem, SHORT_WAIT_MS);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(
      finisher, w, done, finish_result,
      (submit_mode == StreamSubmitMode::COMMIT) ? "wr_stream_commit" : "wr_stream_dtor");

  {
    WritePort::Stream ws(&w, op);
    ws << ConstRawData{TX, sizeof(TX)};
    if (submit_mode == StreamSubmitMode::COMMIT)
    {
      auto ec = ws.Commit();
      ASSERT(ec == expected_result);
    }
  }

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

/**
 * @brief 辅助函数 `VerifyStreamBlockTimeout`。 Helper function `VerifyStreamBlockTimeout`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void VerifyStreamBlockTimeout()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x51, 0x52, 0x53};
  Semaphore sem;
  WriteOperation op(sem, 0);
  WritePort::Stream ws(&w, op);
  ws << ConstRawData{TX, sizeof(TX)};

  auto ec = ws.Commit();
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem.Value() == 0);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);

  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

}  // namespace
