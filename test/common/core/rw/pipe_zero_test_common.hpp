/**
 * @file pipe_zero_test_common.hpp
 * @brief `Pipe` 零长度读写场景测试 helper。 Shared zero-length helpers for `Pipe` tests.
 * @details 测试项目：
 *          1. 提供零长度写请求的完成与后续通路验证 helper。
 *          2. 提供零长度读请求的完成与后续通路验证 helper。
 *          Test items:
 *          1. Provide helpers for zero-length write completion and follow-up transport checks.
 *          2. Provide helpers for zero-length read completion and follow-up transport checks.
 */
#pragma once

#include "pipe_transfer_test_common.hpp"

namespace
{
void VerifyZeroWriteMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  WriteHarness write(mode);
  auto write_result = w(ConstRawData{nullptr, 0}, write.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(write_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(write_result == ErrorCode::OK);
    write.ExpectFinal(ErrorCode::OK);
  }
  ASSERT(w.Size() == 0);

  uint8_t tx = 0x5A;
  uint8_t rx = 0;
  WriteOperation plain_write;
  ReadOperation plain_read;
  ASSERT(w(ConstRawData{&tx, 1}, plain_write) == ErrorCode::OK);
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
}

void VerifyZeroReadMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  uint8_t tx = 0xA7;
  WriteOperation write_op;
  ASSERT(w(ConstRawData{&tx, 1}, write_op) == ErrorCode::OK);

  uint8_t dummy = 0x11;
  ReadHarness read(mode);
  auto zero_result = r(RawData{&dummy, 0}, read.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(zero_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(zero_result == ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  uint8_t rx = 0;
  ReadOperation plain_read;
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
}

}  // namespace
