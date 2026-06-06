/**
 * @file test_stdio_truncation.cpp
 * @brief binding `print` 截断语义子测试。 Split test unit for binding `print` truncation semantics.
 */
#include "print_binding_test_common.hpp"

namespace LibXRBindingPrintTest
{
/**
 * @brief 测试项函数 `TestStdioTruncation`。 Test-item function `TestStdioTruncation`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
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

}  // namespace LibXRBindingPrintTest
