#include <cstring>
#include <iostream>

#include "libxr_format.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "mutex.hpp"

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

  Pipe pipe(64);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();
  Mutex mutex;

  STDIO::write_ = &write;
  STDIO::write_mutex_ = &mutex;
  STDIO::write_stream_ = nullptr;

  uint8_t rx[64] = {0};
  static constexpr char expected[] = "x=+0007 0x2a ok";
  ReadOperation read_op;
  if (read(RawData{rx, sizeof(expected) - 1}, read_op) != ErrorCode::OK)
  {
    return Fail("stdio read arm failed");
  }

  constexpr Format<"x=%+05d %#x %s"> format;
  int written = STDIO::Printf(format, 7, 42U, "ok");
  if (written != static_cast<int>(sizeof(expected) - 1))
  {
    return Fail("stdio format length mismatch");
  }

  read.ProcessPendingReads(false);
  if (std::memcmp(rx, expected, sizeof(expected) - 1) != 0)
  {
    return Fail("stdio format output mismatch");
  }

  STDIO::write_ = nullptr;
  STDIO::write_mutex_ = nullptr;
  STDIO::write_stream_ = nullptr;
  return 0;
}
