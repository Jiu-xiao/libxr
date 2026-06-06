/**
 * @file test_print_binding.cpp
 * @brief binding 平面 STDIO/print 包装层测试。 Binding-plane STDIO/print wrapper tests.
 *
 * 测试项目 / Test items:
 * 1. STDIO `Print` / `Printf` 包装输出。 STDIO print wrappers: verify public `Print`/`Printf` wrappers emit the expected bytes through the bound write port.
 * 2. 超出 sink 容量时的截断行为。 Truncation behavior: verify output larger than the sink capacity is truncated according to the binding-side stream contract.
 *
 * 测试原理 / Test principles:
 * 1. 通过真实 STDIO 流路径驱动，而不是绕过 binding 层直接检查内部缓冲。 Bind print output to the real STDIO stream path instead of bypassing it, because this plane exists specifically to verify host/runtime binding behavior.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "libxr.hpp"
#include "test.hpp"

namespace
{

struct StdioWriteScope
{
  explicit StdioWriteScope(LibXR::WritePort& write, LibXR::Mutex& mutex,
                           LibXR::WritePort::Stream* stream = nullptr)
  {
    LibXR::STDIO::write_ = &write;
    LibXR::STDIO::write_mutex_ = &mutex;
    LibXR::STDIO::write_stream_ = stream;
  }

  StdioWriteScope(const StdioWriteScope&) = delete;
  StdioWriteScope& operator=(const StdioWriteScope&) = delete;

  ~StdioWriteScope()
  {
    LibXR::STDIO::write_ = nullptr;
    LibXR::STDIO::write_mutex_ = nullptr;
    LibXR::STDIO::write_stream_ = nullptr;
  }
};

int Fail(const char* message)
{
  std::cerr << message << '\n';
  std::exit(1);
  return 0;
}

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

void TestStdioTruncation()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  {
    constexpr size_t pipe_capacity = 64;
    constexpr size_t payload_size = pipe_capacity + 16;

    std::array<char, payload_size> payload{};
    for (size_t i = 0; i + 1 < payload.size(); ++i)
    {
      payload[i] = static_cast<char>('a' + (i % 26));
    }
    payload.back() = '\0';

    LibXR::Pipe pipe(pipe_capacity);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    const size_t expected_retained = write.EmptySize();
    StdioWriteScope stdio_scope(write, mutex);

    std::array<uint8_t, payload_size> rx{};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx.data(), expected_retained}, read_op) !=
        LibXR::ErrorCode::OK)
    {
      Fail("stdio pipe-capacity truncation read arm failed");
    }

    int written = LibXR::STDIO::Print<"{}">(payload.data());
    if (written != static_cast<int>(expected_retained))
    {
      Fail("stdio pipe-capacity truncation length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx.data(), payload.data(), expected_retained) != 0)
    {
      Fail("stdio pipe-capacity truncation payload mismatch");
    }
  }

  {
    constexpr size_t stream_capacity = 64;
    constexpr size_t payload_size = stream_capacity + 16;

    std::array<char, payload_size> payload{};
    for (size_t i = 0; i + 1 < payload.size(); ++i)
    {
      payload[i] = static_cast<char>('a' + (i % 26));
    }
    payload.back() = '\0';

    LibXR::Pipe pipe(stream_capacity);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    LibXR::WriteOperation stream_op;
    LibXR::WritePort::Stream stream(&write, stream_op);
    const size_t expected_retained = write.EmptySize();
    StdioWriteScope stdio_scope(write, mutex, &stream);

    std::array<uint8_t, payload_size> rx{};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx.data(), expected_retained}, read_op) !=
        LibXR::ErrorCode::OK)
    {
      Fail("stdio bound-stream truncation read arm failed");
    }

    int written = LibXR::STDIO::Print<"{}">(payload.data());
    if (written != static_cast<int>(expected_retained))
    {
      Fail("stdio bound-stream truncation length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx.data(), payload.data(), expected_retained) != 0)
    {
      Fail("stdio bound-stream truncation payload mismatch");
    }
  }

  {
    constexpr size_t stream_capacity = 4;
    constexpr size_t payload_size = stream_capacity + 16;

    std::array<char, payload_size> payload{};
    for (size_t i = 0; i + 1 < payload.size(); ++i)
    {
      payload[i] = static_cast<char>('a' + (i % 26));
    }
    payload.back() = '\0';

    LibXR::Pipe pipe(stream_capacity);
    LibXR::ReadPort& read = pipe.GetReadPort();
    LibXR::WritePort& write = pipe.GetWritePort();
    LibXR::Mutex mutex;
    LibXR::WriteOperation stream_op;
    LibXR::WritePort::Stream stream(&write, stream_op);
    const size_t expected_retained = write.EmptySize();
    StdioWriteScope stdio_scope(write, mutex, &stream);

    std::array<uint8_t, payload_size> rx{};
    LibXR::ReadOperation read_op;
    if (read(LibXR::RawData{rx.data(), expected_retained}, read_op) !=
        LibXR::ErrorCode::OK)
    {
      Fail("stdio bound-stream small-capacity truncation read arm failed");
    }

    int written = LibXR::STDIO::Print<"{}">(payload.data());
    if (written != static_cast<int>(expected_retained))
    {
      Fail("stdio bound-stream small-capacity truncation length mismatch");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx.data(), payload.data(), expected_retained) != 0)
    {
      Fail("stdio bound-stream small-capacity truncation payload mismatch");
    }
  }
}

}  // namespace

void test_print_binding()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestStdioPrintWrappers();
  TestStdioTruncation();
}
