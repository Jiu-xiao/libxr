/**
 * @file test_stdio_wrappers.cpp
 * @brief STDIO 格式化输出接口测试 / STDIO formatted output interface tests.
 */
#include "linux_stdio_print_test_common.hpp"

namespace LibXRLinuxStdioPrintTest
{
/**
 * @brief 验证 Print 和 Printf 的直接输出与流输出
 *        / Verify direct and stream output through Print and Printf.
 */
void TestStdioPrintWrappers()
{
  // STDIO::Print 直接使用全局写端口。
  // Format direct: `STDIO::Print` writes directly to global `write_`.
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

    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio output mismatch");
    }
  }

  // STDIO::Print 使用绑定的 Stream，输出内容和返回长度保持一致。
  // Format stream-backed: with a bound stream, output still reaches the pipe and returns
  // full length.
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

    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio stream output mismatch");
    }
  }

  // STDIO::Printf 使用全局写端口。
  // Printf direct: printf-literal wrapper uses the same global writer binding.
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

    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio output mismatch");
    }
  }

  // STDIO::Printf 使用绑定的 Stream。
  // Printf stream-backed: printf path must also honor the `write_stream_` binding.
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

    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio stream output mismatch");
    }
  }
}
}  // namespace LibXRLinuxStdioPrintTest
