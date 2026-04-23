#include <cstring>
#include <iostream>

#include "libxr_format.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"

namespace
{
int Fail(const char* message)
{
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main()
{
  using namespace LibXR;

  constexpr Format<"prefix=%+05d payload=0123456789abcdef %#x"> format;
  static constexpr char expected[] = "prefix=+0007 payload=0123456789abcdef 0x2a";

  {
    Pipe pipe(sizeof(expected) - 1);
    ReadPort& read = pipe.GetReadPort();
    WritePort& write = pipe.GetWritePort();
    uint8_t rx[sizeof(expected) - 1] = {0};

    ReadOperation read_op;
    if (read(RawData{rx, sizeof(rx)}, read_op) != ErrorCode::OK)
    {
      return Fail("read arm failed");
    }

    WriteOperation write_op;
    WritePort::Stream stream(&write, write_op);
    if (stream.Write(format, 7, 42U) != ErrorCode::OK)
    {
      return Fail("format stream write failed");
    }
    if (stream.Commit() != ErrorCode::OK)
    {
      return Fail("format stream commit failed");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, expected, sizeof(rx)) != 0)
    {
      return Fail("format stream output mismatch");
    }
  }

  {
    Pipe pipe(8);
    ReadPort& read = pipe.GetReadPort();
    WritePort& write = pipe.GetWritePort();
    uint8_t rx[8] = {0};
    ReadOperation read_op;
    if (read(RawData{rx, sizeof(rx)}, read_op) != ErrorCode::OK)
    {
      return Fail("partial read arm failed");
    }

    WriteOperation write_op;
    WritePort::Stream stream(&write, write_op);

    if (stream.Write(format, 7, 42U) != ErrorCode::FULL)
    {
      return Fail("format stream full-path mismatch");
    }
    if (stream.Commit() != ErrorCode::OK)
    {
      return Fail("format stream partial commit failed");
    }

    read.ProcessPendingReads(false);
    if (std::memcmp(rx, "prefix=+", sizeof(rx)) != 0)
    {
      return Fail("format stream partial prefix mismatch");
    }
  }

  return 0;
}
