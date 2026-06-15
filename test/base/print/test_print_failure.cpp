/**
 * @file test_print_failure.cpp
 * @brief `print` 失败路径语义子测试。 Split test unit for `print` failure-path semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 测试项函数 `TestStreamBackedPrintFailureKeepsPrefix`。 Test-item function `TestStreamBackedPrintFailureKeepsPrefix`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestStreamBackedPrintFailureKeepsPrefix()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  static constexpr char expected[] = "hello ";

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
