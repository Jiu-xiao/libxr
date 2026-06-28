/**
 * @file test_print_failure.cpp
 * @brief `print` stream-backed writer 的失败路径测试。 Failure-path tests for the
 * `print` stream-backed writer.
 * @details 格式对象在写出前缀后失败时，已经进入 stream 的前缀仍可提交；这和纯
 *          bounded-buffer 的失败清理语义不同。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 确认 stream-backed print 失败后保留已写前缀。 Confirm that stream-backed print
 * keeps the prefix written before a later failure.
 */
void TestStreamBackedPrintFailureKeepsPrefix()
{
  static constexpr char expected[] = "hello ";

  // Pipe 读端先挂起，写端 stream 只需要提交成功写出的前缀。
  // Arm the pipe read first; the write stream only has to commit the emitted prefix.
  LibXR::Pipe pipe(64);
  LibXR::ReadPort& read = pipe.GetReadPort();
  LibXR::WritePort& write = pipe.GetWritePort();

  uint8_t rx[sizeof(expected) - 1] = {0};
  LibXR::ReadOperation read_op;
  if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
  {
    Fail("stream-backed print failure read arm failed");
  }

  LibXR::WriteOperation write_op;
  LibXR::WritePort::Stream stream(&write, write_op);
  auto ec = LibXR::Print::Write(stream, PrefixThenBrokenFormat{});
  if (ec != LibXR::ErrorCode::STATE_ERR)
  {
    Fail("stream-backed print failure status mismatch");
  }

  if (stream.Commit() != LibXR::ErrorCode::OK)
  {
    Fail("stream-backed print failure commit mismatch");
  }

  read.ProcessPendingReads(false);
  if (std::memcmp(rx, expected, sizeof(rx)) != 0)
  {
    Fail("stream-backed print failure prefix mismatch");
  }
}

}  // namespace LibXRPrintTest
