/**
 * @file test_print_failure.cpp
 * @brief 流式格式化输出失败后的前缀保留测试
 *        / Prefix retention after stream formatting failures.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 验证格式化失败后仍可提交已写前缀
 *        / Verify an emitted prefix survives a later formatting failure.
 */
void TestStreamBackedPrintFailureKeepsPrefix()
{
  static constexpr char expected[] = "hello ";

  // Pipe 读端先挂起，写入流提交格式化失败前已经写出的前缀。
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

  if (std::memcmp(rx, expected, sizeof(rx)) != 0)
  {
    Fail("stream-backed print failure prefix mismatch");
  }
}

}  // namespace LibXRPrintTest
