/**
 * @file test_stdio_wrappers.cpp
 * @brief Linux STDIO `Print` / `Printf` wrapper 正常写入测试。 Normal-write tests for
 * Linux STDIO `Print` / `Printf` wrappers.
 * @details
 * 1. 每个场景都把 `STDIO::write_` 绑定到 pipe 写端。
 * 2. 从 pipe 读端确认实际字节。
 * 3. stream-backed 场景额外验证 `write_stream_` 绑定。
 */
#include "linux_stdio_print_test_common.hpp"

namespace LibXRLinuxStdioPrintTest
{
/**
 * @brief 覆盖 STDIO 正常写入的 Format/Printf direct 和 stream-backed 路径。 Cover normal
 * STDIO writes through Format/Printf direct and stream-backed paths.
 */
void TestStdioPrintWrappers()
{
  // Format direct：`STDIO::Print` 直接写到全局 `write_`。
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

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio output mismatch");
    }
  }

  // Format stream-backed：绑定 stream 后，输出仍应到同一个 pipe，并返回完整长度。
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

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("format frontend stdio stream output mismatch");
    }
  }

  // Printf direct：printf 字面量 wrapper 走同一个全局写端绑定。
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

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio output mismatch");
    }
  }

  // Printf stream-backed：printf 路径也必须遵守 `write_stream_` 绑定。
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

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      Fail("printf frontend stdio stream output mismatch");
    }
  }
}
}  // namespace LibXRLinuxStdioPrintTest
