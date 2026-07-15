/**
 * @file test_stdio_truncation.cpp
 * @brief Linux STDIO `print` 容量截断语义测试。 Capacity-truncation tests for Linux
 * STDIO `print`.
 * @details
 * 1. 截断时返回实际保留字节数。
 * 2. pipe 中只出现被保留的前缀内容。
 * 3. 覆盖 direct writer、stream-backed writer 和很小 stream 容量。
 */
#include "linux_stdio_print_test_common.hpp"

namespace LibXRLinuxStdioPrintTest
{
/**
 * @brief 覆盖 direct 和 stream-backed STDIO 写入的截断返回值和保留内容。 Cover truncation
 * return values and retained payload for direct and stream-backed STDIO writes.
 */
void TestStdioTruncation()
{
  // Direct writer 容量不足：只保留 pipe 空余容量对应的前缀。
  // Direct writer with insufficient capacity: retain only the prefix that fits the pipe.
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

  // Stream-backed 容量不足：同样按底层写端可用空间截断。
  // Stream-backed insufficient capacity: truncation still follows the underlying writer
  // space.
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

  // 很小 stream 容量：覆盖短缓冲区下的返回值和前缀保留。
  // Very small stream capacity: cover return value and prefix retention for tiny buffers.
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

}  // namespace LibXRLinuxStdioPrintTest
