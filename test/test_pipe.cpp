#include <cstring>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"

//
void test_pipe_basic()
{
  using namespace LibXR;

  // 1) 准备 Pipe 与两端口
  Pipe pipe(/*buffer_size=*/64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  // 2) 测试载荷
  static const uint8_t TX[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t rx[sizeof(TX)] = {0};

  // 3) 操作对象
  ReadOperation rop;   // NOLINT
  WriteOperation wop;  // NOLINT

  // 4) 先提交一个挂起的读请求（等量读取）
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  // 5) 写端写入相同大小的数据
  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  // 6) 如果实现需要主动推进读端，这里再推进一次（幂等）
  r.ProcessPendingReads(/*in_isr=*/false);

  // 7) 校验：读到的内容与写入一致
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

// 追加用例：先写后读（验证缓冲/排队是否正常）
void test_pipe_write_then_read()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {1, 2, 3, 4, 5, 6, 7};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;   // NOLINT
  WriteOperation wop;  // NOLINT

  // 1) 先写入
  ErrorCode ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  // 2) 再提交读请求
  ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  // 3) 主动推进一次，确保读端把缓冲区里的字节拉出来
  r.ProcessPendingReads(false);

  // 4) 比对内容
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

// 追加用例：分块读写（验证跨多次写入仍可一次性读满）
void test_pipe_chunked_rw()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX1[] = {'H', 'e', 'l'};
  static const uint8_t TX2[] = {'l', 'o', ' ', 'X', 'R'};
  uint8_t rx[sizeof(TX1) + sizeof(TX2)] = {0};

  ReadOperation rop;  // NOLINT
  WriteOperation w1;  // NOLINT
  WriteOperation w2;  // NOLINT

  // 挂起一次“整包”读取
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  // 分两次写入
  ec = w(ConstRawData{TX1, sizeof(TX1)}, w1);
  ASSERT(ec == ErrorCode::OK);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, w2);
  ASSERT(ec == ErrorCode::OK);

  // 推进读取
  r.ProcessPendingReads(false);

  // 期望拼成 "Hello XR"
  static const uint8_t EXPECT[] = {'H', 'e', 'l', 'l', 'o', ' ', 'X', 'R'};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_stream_api()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();
  WriteOperation wop;

  uint8_t rx[8] = {0};

  ReadOperation rop;  // NOLINT
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0xAA, 0xBB, 0xCC};
  static const uint8_t B[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};
  ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);

  static const uint8_t EXPECT[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe()
{
  test_pipe_basic();
  test_pipe_write_then_read();
  test_pipe_chunked_rw();
  test_pipe_stream_api();
}
