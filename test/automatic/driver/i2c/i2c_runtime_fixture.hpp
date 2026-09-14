#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

#define CHECK(condition)                                                   \
  do                                                                       \
  {                                                                        \
    if (!(condition))                                                      \
    {                                                                      \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
      std::abort();                                                        \
    }                                                                      \
  } while (0)
#define ASSERT(condition) CHECK(condition)
#define UNUSED(value) (void)(value)

namespace LibXR
{
struct Semaphore
{
  void PostFromCallback(bool) {}
};
template <typename T>
struct Callback
{
  unsigned calls = 0;
  T result{};
  void Run(bool, T value)
  {
    ++calls;
    result = value;
  }
};
#include "i2c_operation.inc"
using ReadOperation = Operation<ErrorCode>;
using WriteOperation = Operation<ErrorCode>;
struct RawData
{
  void* addr_;
  size_t size_;
};
struct ConstRawData
{
  const void* addr_;
  size_t size_;
};
struct I2C
{
  struct Configuration
  {
    uint32_t clock_speed;
  };
  enum class MemAddrLength
  {
    BYTE_8,
    BYTE_16
  };
};
struct TestWaiter
{
  ErrorCode result = ErrorCode::TIMEOUT;
  unsigned posts = 0;
  void Start(Semaphore&)
  {
    posts = 0;
    result = ErrorCode::TIMEOUT;
  }
  void Cancel() {}
  bool TryPost(bool, ErrorCode value)
  {
    ++posts;
    result = value;
    return true;
  }
  ErrorCode Wait(uint32_t) { return result; }
};
namespace Memory
{
inline void FastCopy(void* dst, const void* src, size_t size)
{
  std::memcpy(dst, src, size);
}
}  // namespace Memory
}  // namespace LibXR
using namespace LibXR;
using PollStatus = ReadOperation::OperationPollingStatus;
