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
  TestStdioPrintWrappers();
  TestStdioTruncation();
}
