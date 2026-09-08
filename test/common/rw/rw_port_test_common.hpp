/**
 * @file rw_port_test_common.hpp
 * @brief 读写端口测试辅助函数 / Read and write port test helpers.
 */
#pragma once

#include <cstring>
#include <vector>

#include "rw_thread_test_common.hpp"
#include "test_assert.hpp"

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
 * @brief 验证指定模式下的读完成结果和数据
 *        / Verify read completion and payload for the selected mode.
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
    TEST_ASSERT(block_result == ErrorCode::OK);

    ExpectWaitOk(done);
    JoinThreadIfNeeded(finisher);
    TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    return;
  }

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);
  TEST_ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  {
    auto queue = r.GetReadQueue(false);
    TEST_ASSERT(queue.PushBatch(tx.data(), tx.size()) == ErrorCode::OK);
    queue.Publish();
  }
  if (mode != TestMode::NONE)
  {
    read.ExpectFinal(ErrorCode::OK);
  }
  TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
}

/**
 * @brief 验证指定模式下的写完成结果 / Verify write completion for the selected mode.
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
    TEST_ASSERT(block_result == result);

    ExpectWaitOk(done);
    JoinThreadIfNeeded(finisher);
    TEST_ASSERT(w.Size() == 0);
    return;
  }

  auto call_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
  TEST_ASSERT(call_result == ErrorCode::OK);

  {
    auto queue = w.GetWriteQueue(false);
    TEST_ASSERT(!queue.Empty());
    if (result == ErrorCode::OK)
    {
      static uint8_t sink[16];
      TEST_ASSERT(queue.AvailableSize() <= sizeof(sink));
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
  TEST_ASSERT(w.Size() == 0);
}

}  // namespace
