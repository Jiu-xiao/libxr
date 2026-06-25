/**
 * @file test_stdio_wrappers.cpp
 * @brief linux stdio `print` STDIO 包装层子测试。 Split test unit for linux stdio `print` STDIO wrappers.
 */
#include "linux_stdio_print_test_common.hpp"

namespace LibXRLinuxStdioPrintTest
{
/**
 * @brief 测试项函数 `TestStdioPrintWrappers`。 Test-item function `TestStdioPrintWrappers`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestStdioPrintWrappers()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  {
    static constexpr char expected[] = "x=+0007 0x2a ok";

    LibXR::Pipe pipe(64);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    StdioWriteScope stdio_scope(write, mutex);

    uint8_t rx[sizeof(expected) - 1] = {0};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
    {
      Fail("format frontend stdio read arm failed");
    }

    int written = LibXR::STDIO::Print<"x={:+05d} {:#x} {}">(7, 42U, "ok");
    if (written != static_cast<int>(sizeof(expected) - 1))
    {
      Fail("format frontend stdio length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio output mismatch");
    }
  }

  {
    static constexpr char expected[] = "x=+0007 0x2a ok";

    LibXR::Pipe pipe(64);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    LibXR::WriteOperation stream_op;
    LibXR::WritePort::Stream stream(&write, stream_op);
    StdioWriteScope stdio_scope(write, mutex, &stream);

    uint8_t rx[sizeof(expected) - 1] = {0};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
    {
      Fail("format frontend stdio stream read arm failed");
    }

    int written = LibXR::STDIO::Print<"x={:+05d} {:#x} {}">(7, 42U, "ok");
    if (written != static_cast<int>(sizeof(expected) - 1))
    {
      Fail("format frontend stdio stream length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio stream output mismatch");
    }
  }

  {
    static constexpr char expected[] = "+0007 0x2a ok";

    LibXR::Pipe pipe(64);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    StdioWriteScope stdio_scope(write, mutex);

    uint8_t rx[sizeof(expected) - 1] = {0};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
    {
      Fail("printf frontend stdio read arm failed");
    }

    int written = LibXR::STDIO::Printf<"%+05d %#x %s">(7, 42U, "ok");
    if (written != static_cast<int>(sizeof(expected) - 1))
    {
      Fail("printf frontend stdio length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio output mismatch");
    }
  }

  {
    static constexpr char expected[] = "+0007 0x2a ok";

    LibXR::Pipe pipe(64);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    LibXR::WriteOperation stream_op;
    LibXR::WritePort::Stream stream(&write, stream_op);
    StdioWriteScope stdio_scope(write, mutex, &stream);

    uint8_t rx[sizeof(expected) - 1] = {0};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
    {
      Fail("printf frontend stdio stream read arm failed");
    }

    int written = LibXR::STDIO::Printf<"%+05d %#x %s">(7, 42U, "ok");
    if (written != static_cast<int>(sizeof(expected) - 1))
    {
      Fail("printf frontend stdio stream length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio stream output mismatch");
    }
  }
}
}  // namespace LibXRLinuxStdioPrintTest
