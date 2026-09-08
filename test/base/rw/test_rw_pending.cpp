/**
 * @file test_rw_pending.cpp
 * @brief 异步 RW 完成与容量边界测试 / Asynchronous RW completion and capacity tests.
 */
#include "rw_test_common.hpp"
#include "test_assert.hpp"

/**
 * @brief 验证异步读完成与写错误通知
 *        / Verify asynchronous read completion and write errors.
 */
void test_rw_pending_mode_matrix()
{
  for (auto mode : ASYNC_MODES)
  {
    VerifyPendingReadMode(mode);
    VerifyPendingWriteMode(mode, LibXR::ErrorCode::FAILED);
  }
}

/**
 * @brief 验证零长度操作与满队列拒绝写入
 *        / Verify zero-size operations and full-queue rejection.
 */
void test_rw_edge_cases()
{
  using namespace LibXR;

  for (auto mode : ASYNC_MODES)
  {
    VerifyZeroWriteMode(mode);
    VerifyZeroReadMode(mode);
  }

  WritePort w(1, 4);
  w = PendingWriteFun;
  const uint8_t tx2[] = {5};
  WriteOperation op1;
  WriteOperation op2;
  std::vector<uint8_t> tx1(w.EmptySize(), 0x3C);

  TEST_ASSERT(!tx1.empty());
  TEST_ASSERT(w(ConstRawData{tx1.data(), tx1.size()}, op1) == ErrorCode::OK);
  auto second_result = w(ConstRawData{tx2, sizeof(tx2)}, op2);
  TEST_ASSERT(second_result == ErrorCode::FULL);

  {
    auto queue = w.GetWriteQueue(false);
    TEST_ASSERT(!queue.Empty());
    static uint8_t sink[4];
    queue.PopAll(sink);
  }
}

/**
 * @brief 运行异步完成与边界测试 / Run asynchronous completion and boundary tests.
 */
void RunBaseRwPendingTests()
{
  test_rw_pending_mode_matrix();
  test_rw_edge_cases();
}
